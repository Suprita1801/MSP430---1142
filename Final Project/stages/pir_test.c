/*
 * SmartGym — PIR Motion Sensor Test
 * HC-SR501 + LCD + Buzzer
 *
 * Target : MSP430G2553
 * Clock  : 16 MHz DCO (calibrated)
 *
 * WIRING
 * ─────────────────────────────────────────────────────
 *  HC-SR501            Breadboard
 *  VCC       →         Arduino 5V
 *  ECHI      →         10kΩ → junction → P1.6 (MSP430)
 *                      junction → 20kΩ → GND
 *  GND       →         Common GND
 *
 *  Buzzer (3-pin)      MSP430
 *  VCC       →         3.3V
 *  I/O       →         P1.5  (active-LOW)
 *  GND       →         GND
 *
 *  LCD (PCF8574)       MSP430
 *  SDA       →         P2.3
 *  SCL       →         P2.4
 *
 *  CP2102              MSP430
 *  TXD       →         P1.1
 *  RXD       ←         P1.2
 *  GND       →         GND
 *
 * BEHAVIOUR
 * ─────────────────────────────────────────────────────
 *  POWER ON
 *    LCD Line 1 : "  PIR  TEST     "
 *    LCD Line 2 : "  WARMING UP... "
 *    Wait 30 sec for HC-SR501 warm-up
 *    Then LCD shows "READY"
 *
 *  MOTION DETECTED
 *    LCD Line 1 : "!! MOTION !!"
 *    LCD Line 2 : "USER DETECTED   "
 *    Buzzer     : two short beeps
 *    UART       : "MOTION DETECTED"
 *
 *  NO MOTION (idle for > 5 seconds)
 *    LCD Line 1 : "  MONITORING... "
 *    LCD Line 2 : "  NO MOTION     "
 *    Buzzer     : silent
 *    UART       : "NO MOTION"
 *
 *  NO MOTION for 30 seconds (alert)
 *    LCD Line 1 : "!! ALERT !!     "
 *    LCD Line 2 : "NO MOTION 30S   "
 *    Buzzer     : 4 long pulses
 *    UART       : "ALERT: 30s no motion"
 * ─────────────────────────────────────────────────────
 */

#include <msp430.h>
#include "lcd_i2c.h"
#include <stdint.h>

/* ── Pins ── */
#define PIR_PIN         BIT6            /* P1.6 — input from divider  */
#define BUZZER_PIN      BIT5            /* P1.5 — active-LOW module   */

/* ── Buzzer macros (active-LOW) ── */
#define BUZZER_ON()     (P1OUT &= ~BUZZER_PIN)
#define BUZZER_OFF()    (P1OUT |=  BUZZER_PIN)

/* ── Timing ── */
#define DELAY_MS(x)     __delay_cycles((long)(16000UL * (x)))

/*
 * TimerA: SMCLK/8 = 2MHz, CCR0=49999 → ISR every 25ms
 * 40 fires = 1 second
 */
#define TIMER_CCR0      49999
#define TICKS_PER_SEC   40

/* ── Thresholds ── */
#define WARMUP_SECONDS      30          /* HC-SR501 warm-up time      */
#define NO_MOTION_IDLE      5           /* seconds before "no motion" */
#define NO_MOTION_ALERT     30          /* seconds before alert       */

/* ── States ── */
typedef enum {
    STATE_WARMUP = 0,
    STATE_MONITORING,
    STATE_MOTION,
    STATE_NO_MOTION,
    STATE_ALERT
} PirState;

/* ── Globals ── */
volatile PirState  pirState        = STATE_WARMUP;
volatile uint8_t   timerSubTick    = 0;
volatile uint8_t   timerTick       = 0;
volatile uint16_t  noMotionSeconds = 0;
volatile uint16_t  warmupSeconds   = 0;
volatile uint8_t   stateChanged    = 0;

/* ════════════════════════════════════════════════════
 *  UART
 * ════════════════════════════════════════════════════ */
void uart_init(void)
{
    P1SEL  |= BIT1 + BIT2;
    P1SEL2 |= BIT1 + BIT2;
    UCA0CTL1 |= UCSSEL_2;
    UCA0BR0   = 0x82;
    UCA0BR1   = 0x06;
    UCA0MCTL  = UCBRS_6;
    UCA0CTL1 &= ~UCSWRST;
}

void uart_send_char(char c)
{
    while (!(IFG2 & UCA0TXIFG));
    UCA0TXBUF = c;
}

void uart_print(const char *str)
{
    while (*str) uart_send_char(*str++);
}

void uart_print_int(uint16_t n)
{
    char buf[6];
    uint8_t i = 0;
    if (n == 0) { uart_send_char('0'); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i--) uart_send_char(buf[i]);
}

/* ════════════════════════════════════════════════════
 *  BUZZER
 * ════════════════════════════════════════════════════ */
void buzzer_init(void)
{
    P1DIR |=  BUZZER_PIN;
    P1OUT |=  BUZZER_PIN;   /* HIGH = OFF for active-LOW module */
}

/* Two short beeps — motion detected */
void buzzer_motion(void)
{
    BUZZER_ON();  DELAY_MS(120);
    BUZZER_OFF(); DELAY_MS(80);
    BUZZER_ON();  DELAY_MS(120);
    BUZZER_OFF();
}

/* Four long pulses — 30s no motion alert */
void buzzer_alert(void)
{
    uint8_t i;
    for (i = 0; i < 4; i++)
    {
        BUZZER_ON();  DELAY_MS(400);
        BUZZER_OFF(); DELAY_MS(200);
    }
}

/* ════════════════════════════════════════════════════
 *  PIR
 * ════════════════════════════════════════════════════ */
void pir_init(void)
{
    P1DIR &= ~PIR_PIN;
    P1REN &= ~PIR_PIN;      /* no pull resistor — divider sets level */
}

uint8_t pir_detected(void)
{
    return (P1IN & PIR_PIN) ? 1 : 0;
}

/* ════════════════════════════════════════════════════
 *  TIMER
 * ════════════════════════════════════════════════════ */
void timer_init(void)
{
    TA0CTL   = TASSEL_2 | ID_3 | MC_1; /* SMCLK/8, UP mode           */
    TA0CCR0  = TIMER_CCR0;
    TA0CCTL0 = CCIE;
}

/* ════════════════════════════════════════════════════
 *  LCD HELPERS
 * ════════════════════════════════════════════════════ */
void lcd_print_padded(const char *str)
{
    uint8_t i = 0;
    while (str[i] && i < 16) { LCD_printChar(str[i]); i++; }
    while (i < 16)            { LCD_printChar(' ');    i++; }
}

void lcd_show(const char *line1, const char *line2)
{
    LCD_setCursor(0, 0); lcd_print_padded(line1);
    LCD_setCursor(0, 1); lcd_print_padded(line2);
}

/* Print seconds as countdown on Line 2 */
void lcd_show_warmup(uint16_t remaining)
{
    LCD_setCursor(0, 0); lcd_print_padded("  PIR  TEST     ");
    LCD_setCursor(0, 1);
    LCD_print("WARMUP ");
    uart_print_int(remaining);           /* reuse int printer for LCD  */

    /* Print digit on LCD directly */
    uint8_t tens = remaining / 10;
    uint8_t ones = remaining % 10;
    if (tens > 0) LCD_printChar('0' + tens);
    else          LCD_printChar(' ');
    LCD_printChar('0' + ones);
    LCD_print("s LEFT      ");
}

/* Show no-motion counter on Line 2 */
void lcd_show_no_motion(uint16_t seconds)
{
    LCD_setCursor(0, 0); lcd_print_padded("  MONITORING... ");
    LCD_setCursor(0, 1);
    LCD_print("NO MOV ");
    uint8_t tens = (seconds % 100) / 10;
    uint8_t ones = seconds % 10;
    LCD_printChar('0' + tens);
    LCD_printChar('0' + ones);
    LCD_print("s        ");
}

/* ════════════════════════════════════════════════════
 *  MAIN
 * ════════════════════════════════════════════════════ */
void main(void)
{
    WDTCTL = WDTPW | WDTHOLD;

    /* Buzzer OFF before clock init */
    P1DIR = BUZZER_PIN;
    P1OUT = BUZZER_PIN;

    BCSCTL1 = CALBC1_16MHZ;
    DCOCTL  = CALDCO_16MHZ;

    /* Software I2C idle HIGH */
    P2DIR |= BIT3 + BIT4;
    P2OUT |= BIT3 + BIT4;

    uart_init();
    buzzer_init();
    pir_init();
    timer_init();
    LCD_init();
    LCD_clear();

    __bis_SR_register(GIE);

    /* Startup screen */
    lcd_show("  PIR  TEST     ", "  WARMING UP... ");
    uart_print("=== PIR Motion Test ===\r\n");
    uart_print("Warming up HC-SR501...\r\n\r\n");

    /* ══════════════════════════════════════════════
     *  MAIN LOOP
     * ══════════════════════════════════════════════ */
    while (1)
    {
        if (timerTick)
        {
            timerTick = 0;

            /* ── Warm-up phase ── */
            if (pirState == STATE_WARMUP)
            {
                warmupSeconds++;
                uint16_t remaining = WARMUP_SECONDS - warmupSeconds;
                lcd_show_warmup(remaining);

                if (warmupSeconds >= WARMUP_SECONDS)
                {
                    pirState = STATE_MONITORING;
                    lcd_show("  MONITORING... ", "  READY         ");
                    uart_print("Warm-up complete | Monitoring started\r\n\r\n");
                    DELAY_MS(1000);
                }
            }

            /* ── Monitoring phase ── */
            else
            {
                if (pir_detected())
                {
                    /* Motion detected — reset counter */
                    noMotionSeconds = 0;

                    if (pirState != STATE_MOTION)
                    {
                        pirState = STATE_MOTION;

                        /* LCD update */
                        lcd_show("!! MOTION !!    ", "USER DETECTED   ");

                        /* Buzzer */
                        buzzer_motion();

                        /* UART */
                        uart_print("MOTION DETECTED\r\n\r\n");
                    }
                }
                else
                {
                    /* No motion */
                    noMotionSeconds++;

                    if (noMotionSeconds >= NO_MOTION_ALERT)
                    {
                        /* 30s alert */
                        if (pirState != STATE_ALERT)
                        {
                            pirState = STATE_ALERT;

                            lcd_show("!! ALERT !!     ", "NO MOTION 30S   ");
                            buzzer_alert();

                            uart_print("ALERT: No motion for ");
                            uart_print_int(noMotionSeconds);
                            uart_print("s\r\n\r\n");

                            noMotionSeconds = 0;   /* reset after alert */
                        }
                    }
                    else if (noMotionSeconds >= NO_MOTION_IDLE)
                    {
                        /* Idle — show counter */
                        if (pirState != STATE_NO_MOTION)
                        {
                            pirState = STATE_NO_MOTION;
                            uart_print("NO MOTION\r\n\r\n");
                        }
                        lcd_show_no_motion(noMotionSeconds);
                    }
                }
            }
        }

        __bis_SR_register(LPM0_bits);
    }
}

/* ════════════════════════════════════════════════════
 *  TIMER A0 ISR — fires every 25ms
 *  40 × 25ms = 1 second
 * ════════════════════════════════════════════════════ */
#pragma vector = TIMER0_A0_VECTOR
__interrupt void Timer_A0_ISR(void)
{
    timerSubTick++;
    if (timerSubTick >= TICKS_PER_SEC)
    {
        timerSubTick = 0;
        timerTick    = 1;
        __bic_SR_register_on_exit(LPM0_bits);
    }
}

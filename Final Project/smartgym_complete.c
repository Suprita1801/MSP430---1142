/*
 * ═══════════════════════════════════════════════════════════════
 *  SmartGym Monitor — COMPLETE FINAL CODE
 *
 *  Components:
 *  1. 5 Push Buttons   (Entry, Exit, Mode, Confirm, Emergency)
 *  2. I2C LCD 16x2     (PCF8574 backpack)
 *  3. Buzzer           (3-pin active-LOW module)
 *  4. Session Timer    (TimerA software prescaler)
 *  5. TMP36            (Temperature sensor)
 *  6. HC-SR501 PIR     (Motion sensor via voltage divider)
 *  7. HM-10 BLE        (Bluetooth to LightBlue app)
 *  8. CP2102           (Debug UART — kept for verification)
 *
 *  Target  : MSP430G2553
 *  Clock   : 16 MHz DCO (calibrated)
 * ═══════════════════════════════════════════════════════════════
 *
 * COMPLETE PIN MAP
 * ───────────────────────────────────────────────────────────────
 *  PIN    COMPONENT          NOTES
 *  P1.0   TMP36 VOUT         ADC A0
 *  P1.1   CP2102 TXD         MSP RX  — debug UART
 *  P1.2   CP2102 RXD         MSP TX  — debug UART
 *  P1.3   Entry Button       → GND   (pull-up)
 *  P1.4   Exit  Button       → 3.3V  (pull-down)
 *  P1.5   Buzzer I/O         3-pin active-LOW
 *  P1.6   PIR ECHI           via 10kΩ/20kΩ voltage divider
 *  P2.0   Mode Button        → GND   (pull-up)
 *  P2.1   Confirm Button     → GND   (pull-up)
 *  P2.2   Emergency Button   → GND   (pull-up)
 *  P2.3   LCD SDA            Software I2C
 *  P2.4   LCD SCL            Software I2C
 *  P2.5   HM-10 RXD          Software UART TX → BLE
 *
 * BUZZER  VCC→3.3V  I/O→P1.5  GND→GND  (active-LOW)
 * TMP36   VCC→3.3V  OUT→P1.0  GND→GND
 * PIR     VCC→5V(Arduino)  ECHI→10k→[junction+P1.6]→20k→GND
 * HM-10   VCC→3.3V  GND→GND  RXD→P2.5  TXD→unconnected
 *
 * SESSION DURATIONS
 * ───────────────────────────────────────────────────────────────
 *  CARDIO : 20 min | warn 60s left  | 5 min rest
 *  UPPER  : 10 min | 5+5 sides      | warn 4 min left | 3 min rest
 *  LOWER  : 15 min | warn 60s left  | 3 min rest
 *
 * TEMPERATURE
 * ───────────────────────────────────────────────────────────────
 *  < 20°C  → COLD!
 *  20-25°C → GOOD
 *  > 25°C  → TURN ON AC
 *
 * BLUETOOTH MESSAGES (LightBlue app FFE1 characteristic)
 * ───────────────────────────────────────────────────────────────
 *  ENTRY|IN_GYM
 *  EXIT|IDLE
 *  MODE:CARDIO
 *  MODE:UPPER
 *  MODE:LOWER
 *  SESSION:ACTIVE
 *  WARN:60S
 *  REST:START
 *  REST:DONE
 *  TEMP:24C|GOOD
 *  TEMP:28C|ON AC
 *  TEMP:18C|COLD
 *  PIR:OK
 *  PIR:ALERT 30S
 *  EMERGENCY!!
 * ═══════════════════════════════════════════════════════════════
 */

#include <msp430.h>
#include "lcd_i2c.h"
#include <stdint.h>

/* ════════════════════════════════════════════════════
 *  PIN DEFINES
 * ════════════════════════════════════════════════════ */
#define BUZZER_PIN      BIT5
#define BUZZER_ON()     (P1OUT &= ~BUZZER_PIN)  /* active-LOW ON      */
#define BUZZER_OFF()    (P1OUT |=  BUZZER_PIN)  /* active-LOW OFF     */

#define PIR_PIN         BIT6            /* P1.6                       */
#define BLE_TX_PIN      BIT5            /* P2.5 → HM-10 RXD          */

#define BTN_ENTRY       BIT3            /* P1.3 pull-up  active-LOW   */
#define BTN_EXIT        BIT4            /* P1.4 pull-down active-HIGH */
#define BTN_MODE        BIT0            /* P2.0 pull-up  active-LOW   */
#define BTN_CONFIRM     BIT1            /* P2.1 pull-up  active-LOW   */
#define BTN_EMERGENCY   BIT2            /* P2.2 pull-up  active-LOW   */

/* ════════════════════════════════════════════════════
 *  TIMING
 * ════════════════════════════════════════════════════ */
#define DELAY_MS(x)     __delay_cycles((long)(16000UL * (x)))
#define DEBOUNCE        800000UL
#define TIMER_CCR0      49999           /* 25ms per ISR at 2MHz       */
#define TICKS_PER_SEC   40              /* 40 × 25ms = 1 second       */
#define BIT_CYCLES      1666            /* 16MHz / 9600 baud          */

/* ════════════════════════════════════════════════════
 *  SESSION DURATIONS (seconds)
 * ════════════════════════════════════════════════════ */
#define CARDIO_SESSION  (20 * 60)
#define CARDIO_REST     (5  * 60)
#define CARDIO_WARN     60

#define UPPER_SESSION   (10 * 60)
#define UPPER_HALF      (5  * 60)
#define UPPER_REST      (3  * 60)
#define UPPER_WARN      (4  * 60)

#define LOWER_SESSION   (10 * 60)    /* 5 min left + 5 min right   */
#define LOWER_HALF      (5  * 60)    /* switch side at 5 min       */
#define LOWER_REST      (3  * 60)
#define LOWER_WARN      (4  * 60)   /* warn at 4 min left         */

/* ════════════════════════════════════════════════════
 *  TEMPERATURE THRESHOLDS
 * ════════════════════════════════════════════════════ */
#define TEMP_NORMAL_LOW     20
#define TEMP_NORMAL_HIGH    25
#define TEMP_READ_INTERVAL  10

/* ════════════════════════════════════════════════════
 *  PIR
 * ════════════════════════════════════════════════════ */
#define PIR_NO_MOTION_LIMIT 30

/* ════════════════════════════════════════════════════
 *  TYPES
 * ════════════════════════════════════════════════════ */
typedef enum { MODE_CARDIO=0, MODE_UPPER=1, MODE_LOWER=2 } WorkoutMode;

typedef enum {
    STATE_IDLE=0,
    STATE_MODE_SELECT,
    STATE_ACTIVE,
    STATE_WARNING,
    STATE_REST,
    STATE_EMERGENCY
} GymState;

/* ════════════════════════════════════════════════════
 *  GLOBALS
 * ════════════════════════════════════════════════════ */
volatile GymState    gymState      = STATE_IDLE;
volatile WorkoutMode currentMode   = MODE_CARDIO;
volatile uint8_t     modeConfirmed = 0;

/* Timer */
volatile uint8_t  timerSubTick   = 0;
volatile uint8_t  timerTick      = 0;
volatile uint16_t sessionSeconds = 0;
volatile uint16_t sessionLimit   = 0;
volatile uint16_t warnSeconds    = 0;
volatile uint16_t restSeconds    = 0;
volatile uint16_t restLimit      = 0;
volatile uint8_t  warnBeepTick   = 0;
volatile uint8_t  upperHalfDone  = 0;
volatile uint8_t  lowerHalfDone  = 0;

/* Temperature */
volatile uint8_t  tempReadCounter = 0;
volatile uint8_t  tempReadFlag    = 0;
         int16_t  lastTempC       = 0;

/* PIR */
volatile uint16_t pirNoMotionCount = 0;
volatile uint8_t  pirAlertSent     = 0;

/* ISR flags */
volatile uint8_t entryFlag     = 0;
volatile uint8_t exitFlag      = 0;
volatile uint8_t modeFlag      = 0;
volatile uint8_t confirmFlag   = 0;
volatile uint8_t emergencyFlag = 0;

/* ════════════════════════════════════════════════════
 *  DEBUG UART — CP2102 (hardware UART UCA0)
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

void uart_print_int(int16_t n)
{
    char buf[6];
    uint8_t i = 0;
    if (n < 0) { uart_send_char('-'); n = -n; }
    if (n == 0) { uart_send_char('0'); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i--) uart_send_char(buf[i]);
}

/* ════════════════════════════════════════════════════
 *  BLUETOOTH UART — HM-10 (software UART P2.5)
 * ════════════════════════════════════════════════════ */
void ble_init(void)
{
    P2DIR |=  BLE_TX_PIN;
    P2OUT |=  BLE_TX_PIN;       /* idle HIGH                          */
}

void ble_send_byte(uint8_t byte)
{
    uint8_t i;

    /* Start bit */
    P2OUT &= ~BLE_TX_PIN;
    __delay_cycles(BIT_CYCLES);

    /* 8 data bits LSB first */
    for (i = 0; i < 8; i++)
    {
        if (byte & 0x01) P2OUT |=  BLE_TX_PIN;
        else             P2OUT &= ~BLE_TX_PIN;
        __delay_cycles(BIT_CYCLES);
        byte >>= 1;
    }

    /* Stop bit */
    P2OUT |= BLE_TX_PIN;
    __delay_cycles(BIT_CYCLES);
}

void ble_print(const char *str)
{
    while (*str) ble_send_byte(*str++);
}

void ble_println(const char *str)
{
    ble_print(str);
    ble_send_byte('\r');
    ble_send_byte('\n');
}

void ble_print_int(int16_t n)
{
    char buf[6];
    uint8_t i = 0;
    if (n < 0) { ble_send_byte('-'); n = -n; }
    if (n == 0) { ble_send_byte('0'); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i--) ble_send_byte(buf[i]);
}

/*
 * Send same message to both CP2102 and HM-10
 * Use this for all notifications so trainer
 * sees everything on LightBlue
 */
void notify(const char *str)
{
    uart_print(str);
    uart_print("\r\n");
    ble_println(str);
}

void notify_temp(int16_t t, const char *status)
{
    /* CP2102 */
    uart_print("TEMP:");
    uart_print_int(t);
    uart_print("C|");
    uart_print(status);
    uart_print("\r\n");

    /* HM-10 */
    ble_print("TEMP:");
    ble_print_int(t);
    ble_print("C|");
    ble_println(status);
}

/* ════════════════════════════════════════════════════
 *  ADC10 — TMP36
 * ════════════════════════════════════════════════════ */
void adc_init(void)
{
    /*
     * ADC10 setup for TMP36 on P1.0 (A0)
     * SREF_0     : Vref = VCC (3.3V)
     * ADC10SHT_3 : sample hold 64 ADC clocks — stable for TMP36
     * ADC10ON    : ADC on
     * ADC10IE    : interrupt disabled — polling mode
     * ADC10SR    : slew rate limited — better for slow signals like TMP36
     */
    ADC10CTL1  = INCH_0 | ADC10DIV_3;  /* channel A0, clock /4       */
    ADC10CTL0  = SREF_0                 /* Vref = VCC 3.3V            */
               | ADC10SHT_3             /* 64 clock sample hold       */
               | ADC10SR                /* slow slew rate for TMP36   */
               | ADC10ON;              /* ADC ON                      */
    ADC10AE0   = BIT0;                 /* P1.0 analog enable          */

    /* Allow ADC to settle after power on */
    __delay_cycles(16000 * 10);        /* 10ms settling time          */
}

int16_t adc_read_temp(void)
{
    /*
     * Take 8 samples and average them for stability.
     * TMP36 formula:
     *   Vout_mV = (raw * 3300) / 1023
     *   Temp_C  = (Vout_mV - 500) / 10
     *
     * Use int32_t for intermediate to prevent overflow.
     */
    uint8_t  i;
    int32_t  sum = 0;
    int32_t  vout_mv;

    for (i = 0; i < 8; i++)
    {
        ADC10CTL0 &= ~ENC;             /* disable before config       */
        ADC10CTL0 |=  ENC | ADC10SC;  /* start conversion            */
        while (ADC10CTL1 & ADC10BUSY);/* wait for result             */
        sum += ADC10MEM;
        __delay_cycles(16000);         /* 1ms between samples         */
    }

    sum     = sum / 8;                 /* average of 8 readings       */
    vout_mv = (sum * 3300L) / 1023L;  /* convert to millivolts       */
    return  (int16_t)((vout_mv - 500L) / 10L);
}

/* ════════════════════════════════════════════════════
 *  BUZZER
 * ════════════════════════════════════════════════════ */
void buzzer_init(void)
{
    P1DIR |=  BUZZER_PIN;
    P1OUT |=  BUZZER_PIN;       /* HIGH = OFF active-LOW              */
}

void buzzer_entry(void)         /* pip pip */
{
    BUZZER_ON();  DELAY_MS(100);
    BUZZER_OFF(); DELAY_MS(80);
    BUZZER_ON();  DELAY_MS(100);
    BUZZER_OFF();
}

void buzzer_exit(void)          /* long beep */
{
    BUZZER_ON();  DELAY_MS(600);
    BUZZER_OFF();
}

void buzzer_mode(void)          /* tick */
{
    BUZZER_ON();  DELAY_MS(40);
    BUZZER_OFF();
}

void buzzer_confirm(void)       /* pip pip BEEP */
{
    BUZZER_ON();  DELAY_MS(80);
    BUZZER_OFF(); DELAY_MS(60);
    BUZZER_ON();  DELAY_MS(120);
    BUZZER_OFF(); DELAY_MS(60);
    BUZZER_ON();  DELAY_MS(300);
    BUZZER_OFF();
}

void buzzer_warn(void)          /* 3 slow beeps */
{
    BUZZER_ON();  DELAY_MS(200);
    BUZZER_OFF(); DELAY_MS(100);
    BUZZER_ON();  DELAY_MS(200);
    BUZZER_OFF(); DELAY_MS(100);
    BUZZER_ON();  DELAY_MS(200);
    BUZZER_OFF();
}

void buzzer_temp_alert(void)    /* 2 fast beeps */
{
    BUZZER_ON();  DELAY_MS(100);
    BUZZER_OFF(); DELAY_MS(60);
    BUZZER_ON();  DELAY_MS(100);
    BUZZER_OFF();
}

void buzzer_rest_start(void)    /* double beep */
{
    BUZZER_ON();  DELAY_MS(200);
    BUZZER_OFF(); DELAY_MS(100);
    BUZZER_ON();  DELAY_MS(200);
    BUZZER_OFF();
}

void buzzer_rest_end(void)      /* triple beep */
{
    BUZZER_ON();  DELAY_MS(150);
    BUZZER_OFF(); DELAY_MS(80);
    BUZZER_ON();  DELAY_MS(150);
    BUZZER_OFF(); DELAY_MS(80);
    BUZZER_ON();  DELAY_MS(150);
    BUZZER_OFF();
}

void buzzer_pir_alert(void)     /* 4 long pulses */
{
    uint8_t i;
    for (i = 0; i < 4; i++)
    {
        BUZZER_ON();  DELAY_MS(400);
        BUZZER_OFF(); DELAY_MS(200);
    }
}

void buzzer_emergency(void)     /* rapid continuous */
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        BUZZER_ON();  DELAY_MS(80);
        BUZZER_OFF(); DELAY_MS(50);
    }
}

/* ════════════════════════════════════════════════════
 *  TIMER — SMCLK/8=2MHz, CCR0=49999 → 25ms ISR × 40 = 1s
 * ════════════════════════════════════════════════════ */
void timer_init(void)
{
    TA0CTL   = TASSEL_2 | ID_3 | MC_0;
    TA0CCR0  = TIMER_CCR0;
    TA0CCTL0 = CCIE;
}

void timer_start(void)
{
    timerSubTick = 0;
    TA0CTL |= MC_1;
}

void timer_stop(void)
{
    TA0CTL &= ~MC_3;
}

/* ════════════════════════════════════════════════════
 *  PIR
 * ════════════════════════════════════════════════════ */
void pir_init(void)
{
    P1DIR &= ~PIR_PIN;
    P1REN &= ~PIR_PIN;
}

uint8_t pir_detected(void)
{
    return (P1IN & PIR_PIN) ? 1 : 0;
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

void lcd_show_mode_select(WorkoutMode m)
{
    LCD_setCursor(0, 0); lcd_print_padded("SELECT MODE:    ");
    LCD_setCursor(0, 1);
    switch (m)
    {
        case MODE_CARDIO: lcd_print_padded("> CARDIO        "); break;
        case MODE_UPPER:  lcd_print_padded("> UPPER BODY    "); break;
        case MODE_LOWER:  lcd_print_padded("> LEG TRAIN     "); break;
    }
}

void lcd_print_time(uint16_t total_seconds)
{
    uint8_t mins = total_seconds / 60;
    uint8_t secs = total_seconds % 60;
    LCD_printChar('0' + (mins / 10));
    LCD_printChar('0' + (mins % 10));
    LCD_printChar(':');
    LCD_printChar('0' + (secs / 10));
    LCD_printChar('0' + (secs % 10));
}

void lcd_print_temp(int16_t t)
{
    /* Display as "17 C" — space between number and C for clarity */
    if (t < 0) { LCD_printChar('-'); t = -t; }
    if (t >= 100) LCD_printChar('0' + (t / 100));
    LCD_printChar('0' + ((t % 100) / 10));
    LCD_printChar('0' + (t % 10));
    LCD_printChar(' ');
    LCD_printChar('C');
}

void lcd_show_temp_idle(int16_t t)
{
    /* Format: "TEMP:24 C  GOOD " — 16 chars total */
    LCD_setCursor(0, 1);
    LCD_print("TEMP:");
    lcd_print_temp(t);
    if      (t > TEMP_NORMAL_HIGH) LCD_print(" ON AC");
    else if (t < TEMP_NORMAL_LOW)  LCD_print(" COLD!");
    else                           LCD_print("  GOOD");
    LCD_print(" ");
}

void lcd_show_temp_alert(int16_t t)
{
    /* Format: "TEMP:28 C  ON AC" */
    LCD_setCursor(0, 1);
    LCD_print("TEMP:");
    lcd_print_temp(t);
    if (t > TEMP_NORMAL_HIGH) LCD_print(" ON AC");
    else                      LCD_print(" COLD!");
}

void lcd_update_session(void)
{
    uint16_t remaining = sessionLimit - sessionSeconds;

    LCD_setCursor(0, 0);
    switch (currentMode)
    {
        case MODE_CARDIO: LCD_print("CARDIO "); break;
        case MODE_UPPER:  LCD_print("UPPER  "); break;
        case MODE_LOWER:  LCD_print("LOWER  "); break;
    }
    lcd_print_time(sessionSeconds);
    LCD_print("   ");

    LCD_setCursor(0, 1);
    if (gymState == STATE_WARNING)
    {
        LCD_print("!WARN ");
        lcd_print_time(remaining);
        LCD_print("   ");
    }
    else
    {
        LCD_print("LEFT  ");
        lcd_print_time(remaining);
        LCD_print("   ");
    }
}

void lcd_update_rest(void)
{
    LCD_setCursor(0, 0); lcd_print_padded("  REST PERIOD   ");
    LCD_setCursor(0, 1);
    LCD_print("REST  ");
    lcd_print_time(restSeconds);
    LCD_print("   ");
}

/* ════════════════════════════════════════════════════
 *  TEMPERATURE HANDLER
 * ════════════════════════════════════════════════════ */
void handle_temperature(void)
{
    lastTempC = adc_read_temp();

    if (lastTempC > TEMP_NORMAL_HIGH)
    {
        notify_temp(lastTempC, "ON AC");
        if (gymState == STATE_ACTIVE || gymState == STATE_WARNING)
        {
            buzzer_temp_alert();
            lcd_show_temp_alert(lastTempC);
            DELAY_MS(3000);
            lcd_update_session();
        }
        else if (gymState == STATE_REST)
        {
            buzzer_temp_alert();
            lcd_show_temp_alert(lastTempC);
            DELAY_MS(3000);
            lcd_update_rest();
        }
        else if (gymState == STATE_IDLE)
        {
            lcd_show_temp_idle(lastTempC);
        }
    }
    else if (lastTempC < TEMP_NORMAL_LOW)
    {
        notify_temp(lastTempC, "COLD");
        if (gymState == STATE_IDLE)
            lcd_show_temp_idle(lastTempC);
    }
    else
    {
        notify_temp(lastTempC, "GOOD");
        if (gymState == STATE_IDLE)
            lcd_show_temp_idle(lastTempC);
    }
}

/* ════════════════════════════════════════════════════
 *  SESSION HELPERS
 * ════════════════════════════════════════════════════ */
void session_start(WorkoutMode m)
{
    sessionSeconds   = 0;
    warnBeepTick     = 0;
    tempReadCounter  = 0;
    pirNoMotionCount = 0;
    pirAlertSent     = 0;
    upperHalfDone    = 0;
    lowerHalfDone    = 0;

    switch (m)
    {
        case MODE_CARDIO:
            sessionLimit = CARDIO_SESSION;
            restLimit    = CARDIO_REST;
            warnSeconds  = CARDIO_WARN;
            break;
        case MODE_UPPER:
            sessionLimit = UPPER_SESSION;
            restLimit    = UPPER_REST;
            warnSeconds  = UPPER_WARN;
            break;
        case MODE_LOWER:
            sessionLimit = LOWER_SESSION;
            restLimit    = LOWER_REST;
            warnSeconds  = LOWER_WARN;  /* warn at 4 min left         */
            break;
    }

    gymState = STATE_ACTIVE;
    timer_start();
    lcd_update_session();
    notify("SESSION:ACTIVE");
}

void rest_start(void)
{
    restSeconds = restLimit;
    gymState    = STATE_REST;
    buzzer_rest_start();
    lcd_update_rest();
    notify("REST:START");
}

void reset_to_idle(void)
{
    timer_stop();
    gymState       = STATE_IDLE;
    modeConfirmed  = 0;
    currentMode    = MODE_CARDIO;
    sessionSeconds = 0;
}

/* ════════════════════════════════════════════════════
 *  BUTTON INIT
 * ════════════════════════════════════════════════════ */
void buttons_init(void)
{
    P1DIR &= ~BTN_ENTRY;  P1REN |= BTN_ENTRY;  P1OUT |=  BTN_ENTRY;
    P1IES |=  BTN_ENTRY;  P1IFG &= ~BTN_ENTRY; P1IE  |=  BTN_ENTRY;

    P1DIR &= ~BTN_EXIT;   P1REN |= BTN_EXIT;   P1OUT &= ~BTN_EXIT;
    P1IES &= ~BTN_EXIT;   P1IFG &= ~BTN_EXIT;  P1IE  |=  BTN_EXIT;

    P2DIR &= ~BTN_MODE;      P2REN |= BTN_MODE;      P2OUT |=  BTN_MODE;
    P2IES |=  BTN_MODE;      P2IFG &= ~BTN_MODE;     P2IE  |=  BTN_MODE;

    P2DIR &= ~BTN_CONFIRM;   P2REN |= BTN_CONFIRM;   P2OUT |=  BTN_CONFIRM;
    P2IES |=  BTN_CONFIRM;   P2IFG &= ~BTN_CONFIRM;  P2IE  |=  BTN_CONFIRM;

    P2DIR &= ~BTN_EMERGENCY; P2REN |= BTN_EMERGENCY; P2OUT |=  BTN_EMERGENCY;
    P2IES |=  BTN_EMERGENCY; P2IFG &= ~BTN_EMERGENCY;P2IE  |=  BTN_EMERGENCY;
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
    ble_init();
    adc_init();
    buzzer_init();
    pir_init();
    timer_init();
    LCD_init();
    LCD_clear();
    buttons_init();

    __bis_SR_register(GIE);

    /* Read temp at startup */
    lastTempC = adc_read_temp();

    /* Startup screens */
    LCD_setCursor(0, 0); lcd_print_padded("  SMARTGYM MON  ");
    lcd_show_temp_idle(lastTempC);

    notify("=== SmartGym Monitor FINAL ===");
    notify("All systems ready");

    /* ══════════════════════════════════════════════
     *  MAIN LOOP
     * ══════════════════════════════════════════════ */
    while (1)
    {
        /* ── Temperature read ── */
        if (tempReadFlag)
        {
            tempReadFlag = 0;
            handle_temperature();
        }

        /* ── 1-second tick ── */
        if (timerTick)
        {
            timerTick = 0;

            /* Temperature counter */
            tempReadCounter++;
            if (tempReadCounter >= TEMP_READ_INTERVAL)
            {
                tempReadCounter = 0;
                tempReadFlag    = 1;
            }

            /* ── Active / Warning ── */
            if (gymState == STATE_ACTIVE || gymState == STATE_WARNING)
            {
                sessionSeconds++;
                uint16_t remaining = sessionLimit - sessionSeconds;

                /* Upper body side switch at 5 min */
                if (currentMode == MODE_UPPER    &&
                    sessionSeconds == UPPER_HALF &&
                    !upperHalfDone)
                {
                    upperHalfDone = 1;
                    buzzer_warn();
                    LCD_setCursor(0, 1);
                    lcd_print_padded("SWITCH SIDE NOW!");
                    notify("UPPER:SWITCH SIDE");
                    DELAY_MS(2000);
                }

                /* Lower body side switch at 5 min */
                if (currentMode == MODE_LOWER    &&
                    sessionSeconds == LOWER_HALF &&
                    !lowerHalfDone)
                {
                    lowerHalfDone = 1;
                    buzzer_warn();
                    LCD_setCursor(0, 1);
                    lcd_print_padded("SWITCH LEG NOW! ");
                    notify("LOWER:SWITCH LEG");
                    DELAY_MS(2000);
                }

                /* Enter WARNING */
                if (remaining == warnSeconds && gymState == STATE_ACTIVE)
                {
                    gymState     = STATE_WARNING;
                    warnBeepTick = 0;
                    buzzer_warn();
                    notify("WARN:TIME ALMOST UP");
                }

                /* Warning beep every 10s */
                if (gymState == STATE_WARNING)
                {
                    warnBeepTick++;
                    if (warnBeepTick >= 10)
                    {
                        warnBeepTick = 0;
                        buzzer_warn();
                    }
                }

                /* PIR motion check */
                if (pir_detected())
                {
                    /* Motion seen — reset counter */
                    if (pirNoMotionCount > 5)
                    {
                        /* Only update LCD if we were in no-motion state */
                        lcd_update_session();
                    }
                    pirNoMotionCount = 0;
                    pirAlertSent     = 0;
                }
                else
                {
                    pirNoMotionCount++;

                    /* Show live no-motion counter on LCD line 2 */
                    if (pirNoMotionCount >= 5 && pirNoMotionCount < PIR_NO_MOTION_LIMIT)
                    {
                        LCD_setCursor(0, 1);
                        LCD_print("NO MOV ");
                        uint8_t s = (uint8_t)pirNoMotionCount;
                        LCD_printChar('0' + (s / 10));
                        LCD_printChar('0' + (s % 10));
                        LCD_print("s LEFT  ");
                    }

                    if (pirNoMotionCount >= PIR_NO_MOTION_LIMIT && !pirAlertSent)
                    {
                        pirAlertSent = 1;
                        buzzer_pir_alert();
                        LCD_setCursor(0, 1);
                        lcd_print_padded("NO MOTION DETCT!");
                        notify("PIR:NO MOTION 30S ALERT");
                        DELAY_MS(3000);
                        lcd_update_session();
                        pirNoMotionCount = 0;
                    }
                }

                /* Session complete */
                if (sessionSeconds >= sessionLimit)
                {
                    timer_stop();
                    rest_start();
                }
                else
                {
                    lcd_update_session();

                    /*
                     * PIR live status on Line 2 every 5 seconds
                     * Only during ACTIVE (not WARNING — warn uses Line 2)
                     * Shows "PIR: MOTION OK " or "PIR: NO MOTION "
                     * for 1 second then returns to time display
                     */
                    if (gymState == STATE_ACTIVE && (sessionSeconds % 5 == 0))
                    {
                        LCD_setCursor(0, 1);
                        if (pir_detected())
                            lcd_print_padded("PIR: MOTION OK  ");
                        else
                            lcd_print_padded("PIR: NO MOTION  ");
                        DELAY_MS(1000);
                        lcd_update_session();
                    }
                }
            }

            /* ── Rest countdown ── */
            else if (gymState == STATE_REST)
            {
                if (restSeconds > 0) restSeconds--;

                if (restSeconds == 0)
                {
                    gymState = STATE_IDLE;
                    buzzer_rest_end();
                    LCD_setCursor(0, 0); lcd_print_padded("  REST COMPLETE ");
                    lcd_show_temp_idle(lastTempC);
                    notify("REST:DONE|PRESS ENTRY");
                }
                else
                {
                    lcd_update_rest();
                }
            }

            /* ── IDLE: refresh temp ── */
            else if (gymState == STATE_IDLE)
            {
                lcd_show_temp_idle(lastTempC);
            }
        }

        /* ══ BUTTON HANDLERS ════════════════════════ */

        /* EMERGENCY */
        if (emergencyFlag)
        {
            emergencyFlag = 0;
            timer_stop();
            gymState      = STATE_EMERGENCY;
            modeConfirmed = 0;

            lcd_show("!! EMERGENCY !!", " HELP IS COMING ");
            buzzer_emergency();
            notify("EMERGENCY!!");

            /*
             * Auto-reset after 10 seconds
             * Buzzer sounds twice more during countdown
             * then system returns to IDLE so user can re-enter
             */
            DELAY_MS(3000);
            buzzer_emergency();         /* second alert               */
            DELAY_MS(3000);
            buzzer_emergency();         /* third alert                */
            DELAY_MS(2000);

            /* Countdown on LCD */
            lcd_show("EMERGENCY RESET ", "RESTARTING...   ");
            notify("EMERGENCY:AUTO RESET");
            DELAY_MS(2000);

            /* Reset to IDLE */
            gymState      = STATE_IDLE;
            modeConfirmed = 0;
            currentMode   = MODE_CARDIO;
            sessionSeconds = 0;

            LCD_setCursor(0, 0); lcd_print_padded("  SYSTEM RESET  ");
            lcd_show_temp_idle(lastTempC);
            notify("SYSTEM:READY|PRESS ENTRY");
        }

        /* ENTRY */
        else if (entryFlag)
        {
            entryFlag = 0;
            if (gymState == STATE_IDLE)
            {
                gymState      = STATE_MODE_SELECT;
                modeConfirmed = 0;
                currentMode   = MODE_CARDIO;
                buzzer_entry();
                lcd_show_mode_select(currentMode);
                notify("ENTRY|IN_GYM");
            }
            else { notify("ENTRY:IGNORED"); }
        }

        /* EXIT */
        else if (exitFlag)
        {
            exitFlag = 0;
            if (gymState == STATE_MODE_SELECT ||
                gymState == STATE_ACTIVE      ||
                gymState == STATE_WARNING     ||
                gymState == STATE_REST)
            {
                reset_to_idle();
                buzzer_exit();
                LCD_setCursor(0, 0); lcd_print_padded("  GYM IS FREE   ");
                lcd_show_temp_idle(lastTempC);
                notify("EXIT|IDLE");
            }
            else { notify("EXIT:IGNORED"); }
        }

        /* MODE */
        else if (modeFlag)
        {
            modeFlag = 0;
            if (gymState == STATE_MODE_SELECT && !modeConfirmed)
            {
                currentMode = (currentMode + 1) % 3;
                buzzer_mode();
                lcd_show_mode_select(currentMode);
                switch (currentMode)
                {
                    case MODE_CARDIO: notify("MODE:CARDIO"); break;
                    case MODE_UPPER:  notify("MODE:UPPER");  break;
                    case MODE_LOWER:  notify("MODE:LOWER");  break;
                }
            }
            else { notify("MODE:IGNORED"); }
        }

        /* CONFIRM */
        else if (confirmFlag)
        {
            confirmFlag = 0;
            if (gymState == STATE_MODE_SELECT && !modeConfirmed)
            {
                modeConfirmed = 1;

                LCD_setCursor(0, 0); lcd_print_padded("MODE CONFIRMED! ");
                LCD_setCursor(0, 1);
                switch (currentMode)
                {
                    case MODE_CARDIO: lcd_print_padded("  CARDIO 20 MIN "); break;
                    case MODE_UPPER:  lcd_print_padded(" UPPER 5+5=10MIN"); break;
                    case MODE_LOWER:  lcd_print_padded(" LOWER 5+5=10MIN"); break;
                }

                buzzer_confirm();
                DELAY_MS(1000);
                session_start(currentMode);
            }
            else { notify("CONFIRM:IGNORED"); }
        }

        __bis_SR_register(LPM0_bits);
    }
}

/* ════════════════════════════════════════════════════
 *  TIMER A0 ISR — every 25ms → software count to 1s
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

/* ════════════════════════════════════════════════════
 *  PORT 1 ISR — Entry (P1.3) + Exit (P1.4)
 * ════════════════════════════════════════════════════ */
#pragma vector = PORT1_VECTOR
__interrupt void Port1_ISR(void)
{
    __delay_cycles(DEBOUNCE);

    if (P1IFG & BTN_ENTRY)
    {
        if (!(P1IN & BTN_ENTRY)) { entryFlag = 1; __bic_SR_register_on_exit(LPM0_bits); }
        P1IFG &= ~BTN_ENTRY;
    }
    if (P1IFG & BTN_EXIT)
    {
        if (P1IN & BTN_EXIT)     { exitFlag  = 1; __bic_SR_register_on_exit(LPM0_bits); }
        P1IFG &= ~BTN_EXIT;
    }
}

/* ════════════════════════════════════════════════════
 *  PORT 2 ISR — Mode + Confirm + Emergency
 * ════════════════════════════════════════════════════ */
#pragma vector = PORT2_VECTOR
__interrupt void Port2_ISR(void)
{
    __delay_cycles(DEBOUNCE);

    if (P2IFG & BTN_MODE)
    {
        if (!(P2IN & BTN_MODE))      { modeFlag      = 1; __bic_SR_register_on_exit(LPM0_bits); }
        P2IFG &= ~BTN_MODE;
    }
    if (P2IFG & BTN_CONFIRM)
    {
        if (!(P2IN & BTN_CONFIRM))   { confirmFlag   = 1; __bic_SR_register_on_exit(LPM0_bits); }
        P2IFG &= ~BTN_CONFIRM;
    }
    if (P2IFG & BTN_EMERGENCY)
    {
        if (!(P2IN & BTN_EMERGENCY)) { emergencyFlag = 1; __bic_SR_register_on_exit(LPM0_bits); }
        P2IFG &= ~BTN_EMERGENCY;
    }
}

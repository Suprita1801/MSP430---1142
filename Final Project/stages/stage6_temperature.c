/*
 * SmartGym Monitor — Stage 6
 * Buttons + LCD + Buzzer (P1.5) + Timer + Temperature (TMP36 on P1.0)
 *
 * Target : MSP430G2553
 * Clock  : 16 MHz DCO (calibrated)
 *
 * WIRING
 * ─────────────────────────────────────────────────────
 *  TMP36               MSP430
 *  VCC (+)   →         3.3V
 *  VOUT      →         P1.0  (ADC A0)
 *  GND (-)   →         GND
 *
 *  All other wiring same as Stage 5
 *  Buzzer    →         P1.5
 *  LCD SDA   →         P2.3
 *  LCD SCL   →         P2.4
 *
 * TMP36 FORMULA
 * ─────────────────────────────────────────────────────
 *  VOUT (mV) = 10 * Temperature(°C) + 500
 *  Temperature(°C) = (VOUT_mV - 500) / 10
 *
 *  ADC10, 10-bit, Vref = 3.3V (VCC)
 *  raw_adc ranges 0..1023 → 0..3300 mV
 *  VOUT_mV = (raw_adc * 3300) / 1023
 *  Temp_C  = (VOUT_mV - 500) / 10
 *
 * TEMPERATURE ALERTS
 * ─────────────────────────────────────────────────────
 *  < 15°C  : "TOO COLD"  — notify to warm up the gym
 *  15–35°C : Normal      — show temperature on LCD
 *  > 35°C  : "TOO HOT"   — notify to cool down gym
 *
 * TEMPERATURE DISPLAY
 * ─────────────────────────────────────────────────────
 *  IDLE state  : Line 2 shows temperature always
 *  ACTIVE state: Line 2 shows time remaining
 *                Temperature checked every 10s silently
 *                Alert overrides Line 2 for 3 seconds
 *                then returns to session display
 * ─────────────────────────────────────────────────────
 */

#include <msp430.h>
#include "lcd_i2c.h"
#include <stdint.h>

/* ── Buzzer ── */
/*
 * 3-pin buzzer module (VCC, I/O, GND) — active LOW
 * I/O LOW  = buzzer ON
 * I/O HIGH = buzzer OFF
 */
#define BUZZER_PIN   BIT5
#define BUZZER_ON()  (P1OUT &= ~BUZZER_PIN)   /* LOW  = ON  */
#define BUZZER_OFF() (P1OUT |=  BUZZER_PIN)   /* HIGH = OFF */

/* ── Temperature ADC pin ── */
#define TEMP_ADC_INCH   INCH_0          /* A0 = P1.0                  */
#define TEMP_NORMAL_LOW  20             /* below this → too cold      */
#define TEMP_NORMAL_HIGH 25             /* above this → turn on AC    */

/* ── PIR Motion Sensor ── */
#define PIR_PIN         BIT6            /* P1.6 — input from voltage divider  */
#define PIR_NO_MOTION_LIMIT  30         /* seconds without motion → alert     */

/* ── Buttons ── */
#define BTN_ENTRY      BIT3
#define BTN_EXIT       BIT4
#define BTN_MODE       BIT0
#define BTN_CONFIRM    BIT1
#define BTN_EMERGENCY  BIT2

/* ── Timing ── */
#define DEBOUNCE        800000UL
#define DELAY_MS(x)     __delay_cycles((long)(16000UL * (x)))
#define TIMER_CCR0      49999
#define TICKS_PER_SEC   40

/* ── Temperature read interval ── */
#define TEMP_READ_INTERVAL  10          /* read every 10 seconds      */

/* ── Session durations ── */
#define CARDIO_SESSION  (20 * 60)       /* 20 min                     */
#define CARDIO_REST     (5  * 60)       /*  5 min rest                */

/*
 * UPPER BODY: 5 min left arm + 5 min right arm = 10 min total
 * Split tracked via upper_half flag in session logic
 */
#define UPPER_HALF      (5  * 60)       /*  5 min per side            */
#define UPPER_SESSION   (10 * 60)       /* 10 min total               */
#define UPPER_REST      (3  * 60)       /*  3 min rest                */

#define LOWER_SESSION   (15 * 60)       /* 15 min                     */
#define LOWER_REST      (3  * 60)       /*  3 min rest                */

#define CARDIO_WARN     60              /* warn at 60s left           */
#define UPPER_WARN      (4  * 60)       /* warn at 4th min (240s left)*/
#define LOWER_WARN      60              /* warn at 60s left           */

/* ── Modes and states ── */
typedef enum { MODE_CARDIO=0, MODE_UPPER=1, MODE_LOWER=2 } WorkoutMode;

typedef enum {
    STATE_IDLE=0,
    STATE_MODE_SELECT,
    STATE_ACTIVE,
    STATE_WARNING,
    STATE_REST,
    STATE_EMERGENCY
} GymState;

/* ── Globals ── */
volatile GymState    gymState      = STATE_IDLE;
volatile WorkoutMode currentMode   = MODE_CARDIO;
volatile uint8_t     modeConfirmed = 0;

/* Timer */
volatile uint8_t  timerSubTick   = 0;
volatile uint8_t  timerTick      = 0;
volatile uint16_t sessionSeconds = 0;
volatile uint16_t sessionLimit   = 0;
volatile uint16_t restSeconds    = 0;
volatile uint16_t restLimit      = 0;
volatile uint8_t  warnBeepTick   = 0;

/* Upper body half tracking */
volatile uint8_t  upperHalfDone  = 0;   /* 0=left side, 1=right side  */

/* Per-mode warn threshold */
volatile uint16_t warnSeconds    = 60;  /* set per mode in session_start */

/* PIR motion */
volatile uint16_t pirNoMotionCount = 0; /* seconds since last motion detected */
volatile uint8_t  pirAlertSent     = 0; /* prevent repeated alerts            */

/* Temperature */
volatile uint8_t  tempReadCounter = 0;  /* counts seconds 0..9       */
volatile uint8_t  tempReadFlag    = 0;  /* set every TEMP_READ_INTERVAL */
         int16_t  lastTempC       = 0;  /* last measured temperature  */

/* ISR flags */
volatile uint8_t entryFlag     = 0;
volatile uint8_t exitFlag      = 0;
volatile uint8_t modeFlag      = 0;
volatile uint8_t confirmFlag   = 0;
volatile uint8_t emergencyFlag = 0;

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

void uart_print_int(int16_t n)
{
    char buf[6];
    uint8_t i = 0;
    uint8_t neg = 0;
    if (n < 0) { neg = 1; n = -n; }
    if (n == 0) { uart_send_char('0'); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    if (neg) buf[i++] = '-';
    while (i--) uart_send_char(buf[i]);
}

/* ════════════════════════════════════════════════════
 *  ADC10 — TMP36 temperature reading
 * ════════════════════════════════════════════════════ */
void adc_init(void)
{
    /*
     * ADC10 setup:
     * - Channel A0 (P1.0 / TMP36 output)
     * - Reference: VCC (3.3V)
     * - Clock: ADC10OSC
     * - Sample & hold: 64 ADC clocks (stable for TMP36)
     * - Single channel, single conversion
     */
    ADC10CTL1 = TEMP_ADC_INCH;
    ADC10CTL0 = SREF_0          /* Vref = VCC (3.3V)                  */
              | ADC10SHT_3      /* sample hold time: 64 clocks        */
              | ADC10ON;        /* ADC on                             */

    ADC10AE0  = BIT0;           /* P1.0 analog enable                 */
}

/*
 * Read TMP36 and return temperature in °C
 * Formula:
 *   VOUT_mV = (raw * 3300) / 1023
 *   Temp_C  = (VOUT_mV - 500) / 10
 *
 * To avoid float: use integer arithmetic only
 *   Temp_C = ((raw * 3300) / 1023 - 500) / 10
 */
int16_t adc_read_temp(void)
{
    uint16_t raw;
    int32_t  vout_mv;
    int16_t  temp_c;

    ADC10CTL0 |= ENC | ADC10SC;             /* start conversion       */
    while (ADC10CTL1 & ADC10BUSY);          /* wait for completion    */

    raw     = ADC10MEM;                     /* 10-bit result 0..1023  */
    vout_mv = ((int32_t)raw * 3300) / 1023; /* mV                     */
    temp_c  = (int16_t)((vout_mv - 500) / 10);

    return temp_c;
}

/* ════════════════════════════════════════════════════
 *  BUZZER
 * ════════════════════════════════════════════════════ */
void buzzer_init(void)
{
    P1DIR |=  BUZZER_PIN;
    P1OUT |=  BUZZER_PIN;   /* HIGH = OFF for active-LOW module */
}

void buzzer_entry(void)
{
    BUZZER_ON();  DELAY_MS(100);
    BUZZER_OFF(); DELAY_MS(80);
    BUZZER_ON();  DELAY_MS(100);
    BUZZER_OFF();
}

void buzzer_exit(void)
{
    BUZZER_ON();  DELAY_MS(600);
    BUZZER_OFF();
}

void buzzer_mode(void)
{
    BUZZER_ON();  DELAY_MS(40);
    BUZZER_OFF();
}

void buzzer_confirm(void)
{
    BUZZER_ON();  DELAY_MS(80);
    BUZZER_OFF(); DELAY_MS(60);
    BUZZER_ON();  DELAY_MS(120);
    BUZZER_OFF(); DELAY_MS(60);
    BUZZER_ON();  DELAY_MS(300);
    BUZZER_OFF();
}

void buzzer_warn(void)
{
    BUZZER_ON();  DELAY_MS(200);
    BUZZER_OFF(); DELAY_MS(100);
    BUZZER_ON();  DELAY_MS(200);
    BUZZER_OFF(); DELAY_MS(100);
    BUZZER_ON();  DELAY_MS(200);
    BUZZER_OFF();
}

void buzzer_temp_alert(void)    /* two fast beeps — temp warning */
{
    BUZZER_ON();  DELAY_MS(100);
    BUZZER_OFF(); DELAY_MS(60);
    BUZZER_ON();  DELAY_MS(100);
    BUZZER_OFF();
}

void buzzer_rest_start(void)
{
    BUZZER_ON();  DELAY_MS(200);
    BUZZER_OFF(); DELAY_MS(100);
    BUZZER_ON();  DELAY_MS(200);
    BUZZER_OFF();
}

void buzzer_rest_end(void)
{
    BUZZER_ON();  DELAY_MS(150);
    BUZZER_OFF(); DELAY_MS(80);
    BUZZER_ON();  DELAY_MS(150);
    BUZZER_OFF(); DELAY_MS(80);
    BUZZER_ON();  DELAY_MS(150);
    BUZZER_OFF();
}

void buzzer_emergency(void)
{
    uint8_t i;
    for (i = 0; i < 8; i++)
    {
        BUZZER_ON();  DELAY_MS(80);
        BUZZER_OFF(); DELAY_MS(50);
    }
}

/* ════════════════════════════════════════════════════
 *  TIMER
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

/* Print temperature as "XXC" — integer only, no float */
void lcd_print_temp(int16_t t)
{
    if (t < 0)
    {
        LCD_printChar('-');
        t = -t;
    }
    if (t >= 100) LCD_printChar('0' + (t / 100));
    LCD_printChar('0' + ((t % 100) / 10));
    LCD_printChar('0' + (t % 10));
    LCD_printChar('C');
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

/*
 * Show temperature on IDLE screen Line 2
 * "TEMP: 28C  GOOD "
 * "TEMP: 38C  HOT! "
 * "TEMP: 12C  COLD!"
 */
void lcd_show_temp_idle(int16_t t)
{
    LCD_setCursor(0, 1);
    LCD_print("TEMP:");
    lcd_print_temp(t);
    if (t > TEMP_NORMAL_HIGH)       LCD_print(" ON AC ");
    else if (t < TEMP_NORMAL_LOW)   LCD_print(" COLD! ");
    else                            LCD_print("  GOOD ");
}

/*
 * Show temperature alert on Line 2 during active session
 * Overwrites time line briefly
 */
void lcd_show_temp_alert(int16_t t)
{
    LCD_setCursor(0, 1);
    if (t > TEMP_NORMAL_HIGH)
    {
        LCD_print("TEMP:");
        lcd_print_temp(t);
        LCD_print(" ON AC");
    }
    else
    {
        LCD_print("TEMP:");
        lcd_print_temp(t);
        LCD_print(" COLD!");
    }
}

/* ════════════════════════════════════════════════════
 *  TEMPERATURE HANDLER
 *  Called every TEMP_READ_INTERVAL seconds
 * ════════════════════════════════════════════════════ */
void handle_temperature(void)
{
    lastTempC = adc_read_temp();

    uart_print("TEMP: ");
    uart_print_int(lastTempC);
    uart_print("C");

    if (lastTempC > TEMP_HOT)
        uart_print(" | TOO HOT\r\n\r\n");
    else if (lastTempC < TEMP_COLD)
        uart_print(" | TOO COLD\r\n\r\n");
    else
        uart_print(" | NORMAL\r\n\r\n");

    /* IDLE: always show on Line 2 */
    if (gymState == STATE_IDLE)
    {
        lcd_show_temp_idle(lastTempC);
    }

    /* ACTIVE/WARNING: only alert if out of range */
    else if (gymState == STATE_ACTIVE || gymState == STATE_WARNING)
    {
        if (lastTempC > TEMP_HOT || lastTempC < TEMP_COLD)
        {
            buzzer_temp_alert();
            lcd_show_temp_alert(lastTempC);
            DELAY_MS(3000);             /* show alert 3 seconds       */
            lcd_update_session();       /* restore session display    */
        }
    }

    /* REST: show on Line 2 briefly */
    else if (gymState == STATE_REST)
    {
        if (lastTempC > TEMP_HOT || lastTempC < TEMP_COLD)
        {
            buzzer_temp_alert();
            lcd_show_temp_alert(lastTempC);
            DELAY_MS(3000);
            lcd_update_rest();
        }
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
            warnSeconds  = UPPER_WARN;  /* warn at 4th min = 240s left */
            break;
        case MODE_LOWER:
            sessionLimit = LOWER_SESSION;
            restLimit    = LOWER_REST;
            warnSeconds  = LOWER_WARN;
            break;
    }

    gymState = STATE_ACTIVE;
    timer_start();
    lcd_update_session();
}

void rest_start(void)
{
    restSeconds = restLimit;
    gymState    = STATE_REST;
    buzzer_rest_start();
    lcd_update_rest();
    uart_print("SESSION DONE | REST STARTED\r\n\r\n");
}

/* ════════════════════════════════════════════════════
 *  PIR DRIVER
 *  HC-SR501 output → 10kΩ/20kΩ voltage divider → P1.6
 *  Divider: 5V × 20k/(10k+20k) = 3.33V ≈ safe for MSP430
 *  HC-SR501 HIGH = motion detected
 *  HC-SR501 LOW  = no motion
 * ════════════════════════════════════════════════════ */
void pir_init(void)
{
    P1DIR &= ~PIR_PIN;              /* P1.6 input                         */
    P1REN &= ~PIR_PIN;              /* no internal resistor — divider sets level */
}

uint8_t pir_motion_detected(void)
{
    return (P1IN & PIR_PIN) ? 1 : 0;
}

void buzzer_pir_alert(void)     /* long alternating — distinct from all others */
{
    uint8_t i;
    for (i = 0; i < 4; i++)
    {
        BUZZER_ON();  DELAY_MS(400);
        BUZZER_OFF(); DELAY_MS(200);
    }
}

/* ════════════════════════════════════════════════════
 *  BUTTON INIT
 * ════════════════════════════════════════════════════ */
void buttons_init(void)
{
    P1DIR &= ~BTN_ENTRY;  P1REN |=  BTN_ENTRY;  P1OUT |=  BTN_ENTRY;
    P1IES |=  BTN_ENTRY;  P1IFG &= ~BTN_ENTRY;  P1IE  |=  BTN_ENTRY;

    P1DIR &= ~BTN_EXIT;   P1REN |=  BTN_EXIT;   P1OUT &= ~BTN_EXIT;
    P1IES &= ~BTN_EXIT;   P1IFG &= ~BTN_EXIT;   P1IE  |=  BTN_EXIT;

    P2DIR &= ~BTN_MODE;      P2REN |=  BTN_MODE;      P2OUT |=  BTN_MODE;
    P2IES |=  BTN_MODE;      P2IFG &= ~BTN_MODE;      P2IE  |=  BTN_MODE;

    P2DIR &= ~BTN_CONFIRM;   P2REN |=  BTN_CONFIRM;   P2OUT |=  BTN_CONFIRM;
    P2IES |=  BTN_CONFIRM;   P2IFG &= ~BTN_CONFIRM;   P2IE  |=  BTN_CONFIRM;

    P2DIR &= ~BTN_EMERGENCY; P2REN |=  BTN_EMERGENCY; P2OUT |=  BTN_EMERGENCY;
    P2IES |=  BTN_EMERGENCY; P2IFG &= ~BTN_EMERGENCY; P2IE  |=  BTN_EMERGENCY;
}

/* ════════════════════════════════════════════════════
 *  MAIN
 * ════════════════════════════════════════════════════ */
void main(void)
{
    WDTCTL = WDTPW | WDTHOLD;

    /* Buzzer OFF before clock init */
    P1DIR  =  BUZZER_PIN;          /* P1.5 output, rest inputs       */
    P1OUT  =  BUZZER_PIN;          /* HIGH = OFF for active-LOW buzzer */

    BCSCTL1 = CALBC1_16MHZ;
    DCOCTL  = CALDCO_16MHZ;

    P2DIR |= BIT3 + BIT4;
    P2OUT |= BIT3 + BIT4;

    uart_init();
    adc_init();
    buzzer_init();
    pir_init();
    timer_init();
    LCD_init();
    LCD_clear();
    buttons_init();

    __bis_SR_register(GIE);

    /* Read temperature immediately on startup */
    lastTempC = adc_read_temp();

    LCD_setCursor(0, 0); lcd_print_padded("  SMARTGYM MON  ");
    lcd_show_temp_idle(lastTempC);

    uart_print("=== SmartGym Monitor - Stage 6 ===\r\n");
    uart_print("Temperature sensor active on P1.0\r\n");
    uart_print_int(lastTempC);
    uart_print("C at startup\r\n\r\n");

    while (1)
    {
        /* ══ TEMPERATURE READ FLAG ═══════════════════════ */
        if (tempReadFlag)
        {
            tempReadFlag = 0;
            handle_temperature();
        }

        /* ══ 1-SECOND TICK ══════════════════════════════ */
        if (timerTick)
        {
            timerTick = 0;

            /* Temperature counter — ticks every second */
            tempReadCounter++;
            if (tempReadCounter >= TEMP_READ_INTERVAL)
            {
                tempReadCounter = 0;
                tempReadFlag    = 1;
            }

            /* ── Active / Warning session ── */
            if (gymState == STATE_ACTIVE || gymState == STATE_WARNING)
            {
                sessionSeconds++;
                uint16_t remaining = sessionLimit - sessionSeconds;

                if (remaining == WARN_SECONDS && gymState == STATE_ACTIVE)
                {
                    gymState     = STATE_WARNING;
                    warnBeepTick = 0;
                    buzzer_warn();
                    uart_print("WARNING | 60 seconds left\r\n\r\n");
                }

                if (gymState == STATE_WARNING)
                {
                    warnBeepTick++;
                    if (warnBeepTick >= 10)
                    {
                        warnBeepTick = 0;
                        buzzer_warn();
                    }
                }

                /* PIR motion check — only during active session */
                if (pir_motion_detected())
                {
                    pirNoMotionCount = 0;   /* motion seen — reset counter    */
                    pirAlertSent     = 0;
                }
                else
                {
                    pirNoMotionCount++;
                    if (pirNoMotionCount >= PIR_NO_MOTION_LIMIT && !pirAlertSent)
                    {
                        pirAlertSent = 1;
                        buzzer_pir_alert();
                        LCD_setCursor(0, 1); lcd_print_padded("NO MOTION DETCT!");
                        DELAY_MS(3000);
                        lcd_update_session();
                        uart_print("PIR | No motion for 30s | ALERT

");
                    }
                }

                if (sessionSeconds >= sessionLimit)
                {
                    timer_stop();
                    rest_start();
                }
                else
                {
                    lcd_update_session();
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
                    /* Show idle + temp */
                    LCD_setCursor(0, 0); lcd_print_padded("  REST COMPLETE ");
                    lcd_show_temp_idle(lastTempC);
                    uart_print("REST DONE | State: IDLE\r\n\r\n");
                }
                else
                {
                    lcd_update_rest();
                }
            }

            /* ── IDLE: refresh temperature every tick ── */
            else if (gymState == STATE_IDLE)
            {
                lcd_show_temp_idle(lastTempC);
            }
        }

        /* ══ BUTTON HANDLERS ════════════════════════════ */
        if (emergencyFlag)
        {
            emergencyFlag = 0;
            timer_stop();
            gymState = STATE_EMERGENCY;
            modeConfirmed = 0;
            lcd_show("!! EMERGENCY !!", " HELP IS COMING ");
            buzzer_emergency();
            uart_print("!! EMERGENCY !! | State: EMERGENCY\r\n\r\n");
        }

        else if (entryFlag)
        {
            entryFlag = 0;
            if (gymState == STATE_IDLE)
            {
                gymState = STATE_MODE_SELECT;
                modeConfirmed = 0;
                currentMode = MODE_CARDIO;
                buzzer_entry();
                lcd_show_mode_select(currentMode);
                uart_print("ENTRY | State: MODE_SELECT\r\n\r\n");
            }
            else { uart_print("ENTRY ignored\r\n\r\n"); }
        }

        else if (exitFlag)
        {
            exitFlag = 0;
            if (gymState == STATE_MODE_SELECT ||
                gymState == STATE_ACTIVE      ||
                gymState == STATE_WARNING     ||
                gymState == STATE_REST)
            {
                timer_stop();
                gymState       = STATE_IDLE;
                modeConfirmed  = 0;
                currentMode    = MODE_CARDIO;
                sessionSeconds = 0;
                buzzer_exit();
                LCD_setCursor(0, 0); lcd_print_padded("  GYM IS FREE   ");
                lcd_show_temp_idle(lastTempC);
                uart_print("EXIT | State: IDLE\r\n\r\n");
            }
            else { uart_print("EXIT ignored\r\n\r\n"); }
        }

        else if (modeFlag)
        {
            modeFlag = 0;
            if (gymState == STATE_MODE_SELECT && !modeConfirmed)
            {
                currentMode = (currentMode + 1) % 3;
                buzzer_mode();
                lcd_show_mode_select(currentMode);
            }
            else { uart_print("MODE ignored\r\n\r\n"); }
        }

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
                    case MODE_LOWER:  lcd_print_padded("  LOWER  15 MIN "); break;
                }
                buzzer_confirm();
                DELAY_MS(1000);
                session_start(currentMode);
                uart_print("CONFIRM | Session started\r\n\r\n");
            }
            else { uart_print("CONFIRM ignored\r\n\r\n"); }
        }

        __bis_SR_register(LPM0_bits);
    }
}

/* ════════════════════════════════════════════════════
 *  TIMER A0 ISR — every 25ms, software count to 1s
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
 *  PORT 1 ISR
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
 *  PORT 2 ISR
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


/*
 * ═══════════════════════════════════════════════════════════════
 *  SmartGym Monitor — FINAL VERSION v2
 *
 *  Components:
 *  1. 5 Push Buttons   (Entry, Exit, Mode, Confirm, Emergency)
 *  2. I2C LCD 16x2     (PCF8574 backpack — software I2C)
 *  3. Buzzer           (3-pin active-LOW module)
 *  4. Session Timer    (TimerA software prescaler)
 *  5. LMT84            (Temperature sensor)
 *  6. HC-SR501 PIR     (Motion sensor)
 *  7. HM-10 BLE        (Bluetooth to LightBlue app)
 *  8. CP2102           (Debug — software UART)
 *
 *  Target  : MSP430G2553
 *  Clock   : 16 MHz DCO (calibrated)
 * ═══════════════════════════════════════════════════════════════
 *
 * COMPLETE PIN MAP
 * ───────────────────────────────────────────────────────────────
 *  PIN    COMPONENT          NOTES
 *  P1.0   FREE
 *  P1.1   LMT84 VOUT         ADC A1
 *  P1.2   HC-SR501 ECHI      GPIO input via voltage divider
 *  P1.3   Entry Button       → GND   (pull-up)
 *  P1.4   Exit  Button       → 3.3V  (pull-down)
 *  P1.5   Buzzer I/O         3-pin active-LOW
 *  P1.6   CP2102 RXD         Software UART TX
 *  P1.7   CP2102 TXD         Software UART RX (optional)
 *  P2.0   Mode Button        → GND   (pull-up)
 *  P2.1   Confirm Button     → GND   (pull-up)
 *  P2.2   Emergency Button   → GND   (pull-up)
 *  P2.3   LCD SDA            Software I2C
 *  P2.4   LCD SCL            Software I2C
 *  P2.5   HM-10 RXD          Software UART TX → BLE
 *
 * WIRING SUMMARY
 * ───────────────────────────────────────────────────────────────
 *  LMT84   Pin1→3.3V  Pin2→P1.1  Pin3→GND
 *  PIR     VCC→5V(Arduino)  ECHI→10k→[P1.2+20k→GND]
 *  Buzzer  VCC→3.3V  I/O→P1.5  GND→GND  (active-LOW)
 *  HM-10   VCC→3.3V  GND→GND  RXD→P2.5
 *  CP2102  RXD→P1.6  TXD→P1.7  GND→GND
 *  LCD     VCC→3.3V  GND→GND  SDA→P2.3  SCL→P2.4
 *
 * SESSION DURATIONS
 * ───────────────────────────────────────────────────────────────
 *  CARDIO : 20 min | warn 60s left   | 5 min rest
 *  UPPER  : 10 min | 5+5 sides       | warn 4 min left | 3 min rest
 *  LOWER  : 10 min | 5+5 legs        | warn 4 min left | 3 min rest
 *
 * TEMPERATURE (LMT84)
 * ───────────────────────────────────────────────────────────────
 *  < 20°C  → COLD!
 *  20-25°C → GOOD
 *  > 25°C  → TURN ON AC
 *  Formula: mV=(1500×avg)/1023  T=(1035-mV)/5.5
 * ═══════════════════════════════════════════════════════════════
 */

#include <msp430.h>
#include "lcd_i2c.h"

/* ════════════════════════════════════════════════════
 *  PIN DEFINES
 * ════════════════════════════════════════════════════ */

/* Buzzer — 3-pin active-LOW on P1.5 */
#define BUZZER_PIN      BIT5
#define BUZZER_ON()     (P1OUT &= ~BUZZER_PIN)
#define BUZZER_OFF()    (P1OUT |=  BUZZER_PIN)

/* PIR — P1.2 via voltage divider */
#define PIR_PIN         BIT2

/* Software UART — CP2102 debug */
#define DBG_TX_PIN      BIT6            /* P1.6 → CP2102 RXD          */

/* Software UART — HM-10 BLE */
#define BLE_TX_PIN      BIT5            /* P2.5 → HM-10 RXD          */

/* Buttons */
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
#define TIMER_CCR0      49999
#define TICKS_PER_SEC   40
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

#define LOWER_SESSION   (10 * 60)
#define LOWER_HALF      (5  * 60)
#define LOWER_REST      (3  * 60)
#define LOWER_WARN      (4  * 60)

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
volatile unsigned char modeConfirmed = 0;

/* Timer */
volatile unsigned char  timerSubTick   = 0;
volatile unsigned char  timerTick      = 0;
volatile unsigned int   sessionSeconds = 0;
volatile unsigned int   sessionLimit   = 0;
volatile unsigned int   warnSeconds    = 0;
volatile unsigned int   restSeconds    = 0;
volatile unsigned int   restLimit      = 0;
volatile unsigned char  warnBeepTick   = 0;
volatile unsigned char  upperHalfDone  = 0;
volatile unsigned char  lowerHalfDone  = 0;

/* Temperature — same types as reference */
unsigned int  adc_sum, adc_avg;
char          adc_i;
float         adc_mV, adc_T;
int           lastTempC = 0;
volatile unsigned char tempReadCounter = 0;
volatile unsigned char tempReadFlag    = 0;

/* PIR */
volatile unsigned int  pirNoMotionCount = 0;
volatile unsigned char pirAlertSent     = 0;

/* ISR flags */
volatile unsigned char entryFlag     = 0;
volatile unsigned char exitFlag      = 0;
volatile unsigned char modeFlag      = 0;
volatile unsigned char confirmFlag   = 0;
volatile unsigned char emergencyFlag = 0;

/* ════════════════════════════════════════════════════
 *  SOFTWARE UART — DEBUG (P1.6) and BLE (P2.5)
 * ════════════════════════════════════════════════════ */
void uart_init(void)
{
    /* Debug TX on P1.6 */
    P1DIR |=  DBG_TX_PIN;
    P1OUT |=  DBG_TX_PIN;      /* idle HIGH                          */

    /* BLE TX on P2.5 */
    P2DIR |=  BLE_TX_PIN;
    P2OUT |=  BLE_TX_PIN;      /* idle HIGH                          */
}

void dbg_send_byte(unsigned char byte)
{
    unsigned char k;
    P1OUT &= ~DBG_TX_PIN;
    __delay_cycles(BIT_CYCLES);
    for (k = 0; k < 8; k++)
    {
        if (byte & 0x01) P1OUT |=  DBG_TX_PIN;
        else             P1OUT &= ~DBG_TX_PIN;
        __delay_cycles(BIT_CYCLES);
        byte >>= 1;
    }
    P1OUT |= DBG_TX_PIN;
    __delay_cycles(BIT_CYCLES);
}

void ble_send_byte(unsigned char byte)
{
    unsigned char k;
    P2OUT &= ~BLE_TX_PIN;
    __delay_cycles(BIT_CYCLES);
    for (k = 0; k < 8; k++)
    {
        if (byte & 0x01) P2OUT |=  BLE_TX_PIN;
        else             P2OUT &= ~BLE_TX_PIN;
        __delay_cycles(BIT_CYCLES);
        byte >>= 1;
    }
    P2OUT |= BLE_TX_PIN;
    __delay_cycles(BIT_CYCLES);
}

void dbg_print(const char *str)
{
    while (*str) dbg_send_byte(*str++);
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

/* Send to both debug and BLE */
void notify(const char *str)
{
    dbg_print(str);
    dbg_send_byte('\r'); dbg_send_byte('\n');
    ble_println(str);
}

/* Print unsigned int to debug */
void dbg_print_uint(unsigned int n)
{
    char buf[6]; char idx = 0;
    if (n == 0) { dbg_send_byte('0'); return; }
    while (n > 0) { buf[idx++] = '0' + (n % 10); n /= 10; }
    while (idx--) dbg_send_byte(buf[idx]);
}

/* Print float to debug */
void dbg_print_float(float f)
{
    int whole, frac;
    char buf[6]; char idx = 0;
    if (f < 0) { dbg_send_byte('-'); f = -f; }
    whole = (int)f;
    frac  = (int)((f - whole) * 10);
    if (whole == 0) dbg_send_byte('0');
    else { int t=whole; while(t>0){buf[idx++]='0'+(t%10);t/=10;} while(idx--)dbg_send_byte(buf[idx]); idx=0; }
    dbg_send_byte('.');
    dbg_send_byte('0' + frac);
}

/* Print int to BLE */
void ble_print_int(int n)
{
    char buf[6]; char idx = 0;
    if (n < 0) { ble_send_byte('-'); n = -n; }
    if (n == 0) { ble_send_byte('0'); return; }
    while (n > 0) { buf[idx++] = '0' + (n % 10); n /= 10; }
    while (idx--) ble_send_byte(buf[idx]);
}

/* ════════════════════════════════════════════════════
 *  ADC — LMT84 on P1.1 (A1)
 *  Exact reference formula:
 *  mV = (1500.0 × average) / 1023
 *  T  = (1035 - mV) / 5.5
 * ════════════════════════════════════════════════════ */
void adc_init(void)
{
    ADC10CTL0 = SREF_1 + REFON + ADC10ON + ADC10SHT_3;
    ADC10CTL1 = INCH_1;            /* A1 = P1.1                      */
    ADC10AE0 |= BIT1;              /* P1.1 analog enable             */
}

int adc_read_temp(void)
{
    adc_sum = 0;
    for (adc_i = 0; adc_i < 10; adc_i++)
    {
        ADC10CTL0 |= ENC + ADC10SC;
        __delay_cycles(100000);
        adc_sum = adc_sum + ADC10MEM;
    }
    adc_avg = adc_sum / 10;
    adc_mV  = (1500.0 * adc_avg) / 1023;
    adc_T   = (1035 - adc_mV) / 5.5;
    return (int)adc_T;
}

/* ════════════════════════════════════════════════════
 *  BUZZER
 * ════════════════════════════════════════════════════ */
void buzzer_init(void)
{
    P1DIR |=  BUZZER_PIN;
    P1OUT |=  BUZZER_PIN;
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

void buzzer_temp_alert(void)
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

void buzzer_pir_alert(void)
{
    unsigned char k;
    for (k = 0; k < 4; k++)
    {
        BUZZER_ON();  DELAY_MS(400);
        BUZZER_OFF(); DELAY_MS(200);
    }
}

void buzzer_emergency(void)
{
    unsigned char k;
    for (k = 0; k < 8; k++)
    {
        BUZZER_ON();  DELAY_MS(80);
        BUZZER_OFF(); DELAY_MS(50);
    }
}

/* ════════════════════════════════════════════════════
 *  TIMER — 25ms ISR × 40 = 1 second
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
 *  PIR — P1.2
 * ════════════════════════════════════════════════════ */
void pir_init(void)
{
    P1DIR &= ~PIR_PIN;
    P1REN &= ~PIR_PIN;
}

unsigned char pir_detected(void)
{
    return (P1IN & PIR_PIN) ? 1 : 0;
}

/* ════════════════════════════════════════════════════
 *  LCD HELPERS
 * ════════════════════════════════════════════════════ */
void lcd_print_padded(const char *str)
{
    unsigned char i = 0;
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

void lcd_print_time(unsigned int total_seconds)
{
    unsigned char mins = total_seconds / 60;
    unsigned char secs = total_seconds % 60;
    LCD_printChar('0' + (mins / 10));
    LCD_printChar('0' + (mins % 10));
    LCD_printChar(':');
    LCD_printChar('0' + (secs / 10));
    LCD_printChar('0' + (secs % 10));
}

/* Print temperature as "27 C" */
void lcd_print_temp(int t)
{
    if (t < 0) { LCD_printChar('-'); t = -t; }
    if (t >= 100) LCD_printChar('0' + (t / 100));
    LCD_printChar('0' + ((t % 100) / 10));
    LCD_printChar('0' + (t % 10));
    LCD_printChar(' ');
    LCD_printChar('C');
}

void lcd_show_temp_idle(int t)
{
    LCD_setCursor(0, 1);
    LCD_print("TEMP:");
    lcd_print_temp(t);
    if      (t > TEMP_NORMAL_HIGH) LCD_print(" ON AC ");
    else if (t < TEMP_NORMAL_LOW)  LCD_print(" COLD! ");
    else                           LCD_print("  GOOD ");
}

void lcd_show_temp_alert(int t)
{
    LCD_setCursor(0, 1);
    LCD_print("TEMP:");
    lcd_print_temp(t);
    if (t > TEMP_NORMAL_HIGH) LCD_print(" ON AC");
    else                      LCD_print(" COLD!");
}

void lcd_update_session(void)
{
    unsigned int remaining = sessionLimit - sessionSeconds;
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

    /* Debug print */
    dbg_print("TEMP:");
    dbg_print_float(adc_T);
    dbg_print("C");

    /* BLE notify */
    ble_print("TEMP:");
    ble_print_int(lastTempC);
    ble_print("C|");

    if (lastTempC > TEMP_NORMAL_HIGH)
    {
        dbg_print(" | TURN ON AC\r\n\r\n");
        ble_println("ON AC");
        buzzer_temp_alert();
        if (gymState == STATE_IDLE)
            lcd_show_temp_idle(lastTempC);
        else if (gymState == STATE_ACTIVE || gymState == STATE_WARNING)
        {
            lcd_show_temp_alert(lastTempC);
            DELAY_MS(3000);
            lcd_update_session();
        }
        else if (gymState == STATE_REST)
        {
            lcd_show_temp_alert(lastTempC);
            DELAY_MS(3000);
            lcd_update_rest();
        }
    }
    else if (lastTempC < TEMP_NORMAL_LOW)
    {
        dbg_print(" | COLD\r\n\r\n");
        ble_println("COLD");
        if (gymState == STATE_IDLE) lcd_show_temp_idle(lastTempC);
    }
    else
    {
        dbg_print(" | NORMAL\r\n\r\n");
        ble_println("GOOD");
        if (gymState == STATE_IDLE) lcd_show_temp_idle(lastTempC);
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
            warnSeconds  = LOWER_WARN;
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
    /* P1.3 Entry — pull-up, falling edge */
    P1DIR &= ~BTN_ENTRY;  P1REN |= BTN_ENTRY;  P1OUT |=  BTN_ENTRY;
    P1IES |=  BTN_ENTRY;  P1IFG &= ~BTN_ENTRY; P1IE  |=  BTN_ENTRY;

    /* P1.4 Exit — pull-down, rising edge */
    P1DIR &= ~BTN_EXIT;   P1REN |= BTN_EXIT;   P1OUT &= ~BTN_EXIT;
    P1IES &= ~BTN_EXIT;   P1IFG &= ~BTN_EXIT;  P1IE  |=  BTN_EXIT;

    /* P2.0 Mode — pull-up, falling edge */
    P2DIR &= ~BTN_MODE;      P2REN |= BTN_MODE;      P2OUT |=  BTN_MODE;
    P2IES |=  BTN_MODE;      P2IFG &= ~BTN_MODE;     P2IE  |=  BTN_MODE;

    /* P2.1 Confirm — pull-up, falling edge */
    P2DIR &= ~BTN_CONFIRM;   P2REN |= BTN_CONFIRM;   P2OUT |=  BTN_CONFIRM;
    P2IES |=  BTN_CONFIRM;   P2IFG &= ~BTN_CONFIRM;  P2IE  |=  BTN_CONFIRM;

    /* P2.2 Emergency — pull-up, falling edge */
    P2DIR &= ~BTN_EMERGENCY; P2REN |= BTN_EMERGENCY; P2OUT |=  BTN_EMERGENCY;
    P2IES |=  BTN_EMERGENCY; P2IFG &= ~BTN_EMERGENCY;P2IE  |=  BTN_EMERGENCY;
}

/* ════════════════════════════════════════════════════
 *  MAIN
 * ════════════════════════════════════════════════════ */
int main(void)
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

    /* Startup screen */
    LCD_setCursor(0, 0); lcd_print_padded("  SMARTGYM MON  ");
    lcd_show_temp_idle(lastTempC);

    notify("=== SmartGym Monitor FINAL v2 ===");
    notify("All systems ready");

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

            tempReadCounter++;
            if (tempReadCounter >= TEMP_READ_INTERVAL)
            {
                tempReadCounter = 0;
                tempReadFlag    = 1;
            }

            if (gymState == STATE_ACTIVE || gymState == STATE_WARNING)
            {
                sessionSeconds++;
                unsigned int remaining = sessionLimit - sessionSeconds;

                /* Upper body side switch */
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

                /* Lower body leg switch */
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
                    if (pirNoMotionCount > 5) lcd_update_session();
                    pirNoMotionCount = 0;
                    pirAlertSent     = 0;
                }
                else
                {
                    pirNoMotionCount++;

                    /* Show live counter after 5s */
                    if (pirNoMotionCount >= 5 && pirNoMotionCount < PIR_NO_MOTION_LIMIT)
                    {
                        LCD_setCursor(0, 1);
                        LCD_print("NO MOV ");
                        unsigned char s = (unsigned char)pirNoMotionCount;
                        LCD_printChar('0' + (s / 10));
                        LCD_printChar('0' + (s % 10));
                        LCD_print("s LEFT  ");
                    }

                    /* 30s alert */
                    if (pirNoMotionCount >= PIR_NO_MOTION_LIMIT && !pirAlertSent)
                    {
                        pirAlertSent = 1;
                        buzzer_pir_alert();
                        LCD_setCursor(0, 1);
                        lcd_print_padded("NO MOTION DETCT!");
                        notify("PIR:NO MOTION 30S");
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
                }
            }

            /* Rest countdown */
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
                else lcd_update_rest();
            }

            /* IDLE: refresh temp */
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

            /* Auto reset after 10 seconds */
            DELAY_MS(10000);
            buzzer_exit();
            gymState       = STATE_IDLE;
            modeConfirmed  = 0;
            currentMode    = MODE_CARDIO;
            sessionSeconds = 0;
            LCD_setCursor(0, 0); lcd_print_padded("EMERGENCY RESET ");
            LCD_setCursor(0, 1); lcd_print_padded("  PRESS ENTRY   ");
            notify("EMERGENCY:RESET|PRESS ENTRY");
            DELAY_MS(2000);
            LCD_setCursor(0, 0); lcd_print_padded("  SMARTGYM MON  ");
            lcd_show_temp_idle(lastTempC);
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
            else notify("ENTRY:IGNORED");
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
            else notify("EXIT:IGNORED");
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
            else notify("MODE:IGNORED");
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
            else notify("CONFIRM:IGNORED");
        }

        __bis_SR_register(LPM0_bits);
    }
}

/* ════════════════════════════════════════════════════
 *  TIMER A0 ISR — every 25ms × 40 = 1 second
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


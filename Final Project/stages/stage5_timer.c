/*
 * SmartGym Monitor — Stage 5
 * Buttons + LCD + Buzzer + Session Timer + Warning + Rest
 *
 * Target : MSP430G2553
 * Clock  : 16 MHz DCO (calibrated)
 *
 * WIRING
 * ─────────────────────────────────────────────────────
 *  CP2102              MSP430
 *  TXD       →         P1.1  (MSP RX)
 *  RXD       ←         P1.2  (MSP TX)
 *  GND       →         GND
 *
 *  LCD (PCF8574)
 *  SDA       →         P2.3
 *  SCL       →         P2.4
 *
 *  Buzzer (3-pin)      MSP430
 *  VCC       →         3.3V
 *  I/O       →         P1.0
 *  GND       →         GND
 *
 *  Buzzer (2-pin)      MSP430
 *  +         →         P1.0
 *  -         →         GND
 *
 *  Button      Pin    Other end
 *  Entry       P1.3   GND  (pull-up)
 *  Exit        P1.4   3.3V (pull-down)
 *  Mode        P2.0   GND  (pull-up)
 *  Confirm     P2.1   GND  (pull-up)
 *  Emergency   P2.2   GND  (pull-up)
 *
 * SESSION TIMING
 * ─────────────────────────────────────────────────────
 *  CARDIO  : 20 min session,  5 min rest
 *  UPPER   : 15 min session,  3 min rest
 *  LOWER   : 15 min session,  3 min rest
 *
 * BUZZER EVENTS
 * ─────────────────────────────────────────────────────
 *  Entry             : pip pip
 *  Exit              : beeeeep
 *  Mode cycle        : tick
 *  Confirm           : pip pip BEEP
 *  Warning (60s left): 3 slow beeps every 10s
 *  Rest start        : double beep
 *  Rest end          : triple beep — go again
 *  Emergency         : rapid continuous
 * ─────────────────────────────────────────────────────
 */

#include <msp430.h>
#include "lcd_i2c.h"
#include <stdint.h>

/* ── Buzzer ── */
#define BUZZER_PIN  BIT0            /* P1.0                           */
#define BUZZER_ON()   (P1OUT |=  BUZZER_PIN)
#define BUZZER_OFF()  (P1OUT &= ~BUZZER_PIN)

/* ── Buttons ── */
#define BTN_ENTRY      BIT3
#define BTN_EXIT       BIT4
#define BTN_MODE       BIT0
#define BTN_CONFIRM    BIT1
#define BTN_EMERGENCY  BIT2

/* ── Timing ── */
#define DEBOUNCE        800000UL
#define DELAY_MS(x)     __delay_cycles((long)(16000UL * (x)))

/* ── Session durations (seconds) ── */
#define CARDIO_SESSION  (20 * 60)
#define CARDIO_REST     (5  * 60)
#define UPPER_SESSION   (15 * 60)
#define UPPER_REST      (3  * 60)
#define LOWER_SESSION   (15 * 60)
#define LOWER_REST      (3  * 60)

/* ── Warning threshold ── */
#define WARN_SECONDS    60          /* warn when this many seconds left */

/* ── Workout modes ── */
typedef enum { MODE_CARDIO=0, MODE_UPPER=1, MODE_LOWER=2 } WorkoutMode;

/* ── System states ── */
typedef enum {
    STATE_IDLE=0,
    STATE_MODE_SELECT,
    STATE_ACTIVE,
    STATE_WARNING,              /* last 60s of session                */
    STATE_REST,                 /* rest between sets                  */
    STATE_EMERGENCY
} GymState;

/* ── Globals ── */
volatile GymState    gymState      = STATE_IDLE;
volatile WorkoutMode currentMode   = MODE_CARDIO;
volatile uint8_t     modeConfirmed = 0;

/* Timer counters */
volatile uint16_t sessionSeconds  = 0;   /* counts UP from 0          */
volatile uint16_t sessionLimit    = 0;   /* total session duration     */
volatile uint16_t restSeconds     = 0;   /* counts DOWN to 0          */
volatile uint16_t restLimit       = 0;   /* total rest duration        */
volatile uint8_t  timerTick       = 0;   /* set by WDT ISR every 1s   */
volatile uint8_t  warnBeepTick    = 0;   /* warning beep every 10s    */

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

/* ════════════════════════════════════════════════════
 *  WDT+ INTERVAL TIMER — fires every 1 second
 *  ACLK = 32768 Hz (VLO approximation on G2553)
 *  WDT interval: SMCLK/32768 at 16MHz ≈ not accurate
 *  Use ACLK/32768 = 1 second exactly (if crystal present)
 *  Without crystal: use SMCLK with correct divider
 *  16MHz / 512 = 31250 Hz → use Timer_A instead for accuracy
 * ════════════════════════════════════════════════════ */
void timer_init(void)
{
    /*
     * TimerA0 in UP mode, SMCLK/8 = 2MHz
     * CCR0 = 2000000 → fires every 1 second exactly
     */
    TA0CTL   = TASSEL_2 | ID_3 | MC_1;  /* SMCLK, /8, UP mode         */
    TA0CCR0  = 2000000 - 1;              /* 2MHz → 1 second            */
    TA0CCTL0 = CCIE;                     /* enable CCR0 interrupt       */
}

void timer_stop(void)
{
    TA0CTL &= ~MC_3;                     /* stop timer                 */
    TA0CTL |=  MC_0;
}

void timer_start(void)
{
    TA0CTL |= MC_1;                      /* restart in UP mode         */
}

/* ════════════════════════════════════════════════════
 *  BUZZER PATTERNS
 * ════════════════════════════════════════════════════ */
void buzzer_init(void)
{
    P1DIR |=  BUZZER_PIN;
    P1OUT &= ~BUZZER_PIN;               /* OFF immediately on startup  */
}

/* pip pip — Entry */
void buzzer_entry(void)
{
    BUZZER_ON();  DELAY_MS(100);
    BUZZER_OFF(); DELAY_MS(80);
    BUZZER_ON();  DELAY_MS(100);
    BUZZER_OFF();
}

/* beeeeep — Exit */
void buzzer_exit(void)
{
    BUZZER_ON();  DELAY_MS(600);
    BUZZER_OFF();
}

/* tick — Mode cycle */
void buzzer_mode(void)
{
    BUZZER_ON();  DELAY_MS(40);
    BUZZER_OFF();
}

/* pip pip BEEP — Confirm */
void buzzer_confirm(void)
{
    BUZZER_ON();  DELAY_MS(80);
    BUZZER_OFF(); DELAY_MS(60);
    BUZZER_ON();  DELAY_MS(120);
    BUZZER_OFF(); DELAY_MS(60);
    BUZZER_ON();  DELAY_MS(300);
    BUZZER_OFF();
}

/* beep beep — Rest start (take a break) */
void buzzer_rest_start(void)
{
    BUZZER_ON();  DELAY_MS(200);
    BUZZER_OFF(); DELAY_MS(100);
    BUZZER_ON();  DELAY_MS(200);
    BUZZER_OFF();
}

/* beep beep beep — Rest end (go again) */
void buzzer_rest_end(void)
{
    BUZZER_ON();  DELAY_MS(150);
    BUZZER_OFF(); DELAY_MS(80);
    BUZZER_ON();  DELAY_MS(150);
    BUZZER_OFF(); DELAY_MS(80);
    BUZZER_ON();  DELAY_MS(150);
    BUZZER_OFF();
}

/* beep — Single warning beep (called every 10s during warning phase) */
void buzzer_warn(void)
{
    BUZZER_ON();  DELAY_MS(200);
    BUZZER_OFF(); DELAY_MS(100);
    BUZZER_ON();  DELAY_MS(200);
    BUZZER_OFF(); DELAY_MS(100);
    BUZZER_ON();  DELAY_MS(200);
    BUZZER_OFF();
}

/* BZBZBZBZ — Emergency */
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

/*
 * Display MM:SS from a seconds value
 * Writes exactly 5 characters: "MM:SS"
 */
void lcd_print_time(uint16_t seconds)
{
    uint8_t m = seconds / 60;
    uint8_t s = seconds % 60;

    /* tens of minutes */
    LCD_printChar('0' + (m / 10));
    LCD_printChar('0' + (m % 10));
    LCD_printChar(':');
    /* tens of seconds */
    LCD_printChar('0' + (s / 10));
    LCD_printChar('0' + (s % 10));
}

/*
 * Update the session timer line on LCD
 * Line1: "CARDIO  MM:SS   "
 * Line2: "TIME LEFT MM:SS " or "!! WARNING !!   "
 */
void lcd_update_session(void)
{
    uint16_t remaining = sessionLimit - sessionSeconds;

    /* Line 1 — mode + elapsed */
    LCD_setCursor(0, 0);
    switch (currentMode)
    {
        case MODE_CARDIO: LCD_print("CARDIO  "); break;
        case MODE_UPPER:  LCD_print("UPPER   "); break;
        case MODE_LOWER:  LCD_print("LOWER   "); break;
    }
    lcd_print_time(sessionSeconds);
    LCD_print("   ");

    /* Line 2 — time remaining */
    LCD_setCursor(0, 1);
    if (gymState == STATE_WARNING)
    {
        LCD_print("!! WARN ");
        lcd_print_time(remaining);
        LCD_print("  ");
    }
    else
    {
        LCD_print("LEFT    ");
        lcd_print_time(remaining);
        LCD_print("  ");
    }
}

/* Rest screen: "REST    MM:SS   " */
void lcd_update_rest(void)
{
    LCD_setCursor(0, 0); lcd_print_padded("  REST PERIOD   ");
    LCD_setCursor(0, 1);
    LCD_print("TIME LEFT ");
    lcd_print_time(restSeconds);
    LCD_print(" ");
}

/* ════════════════════════════════════════════════════
 *  SESSION HELPERS
 * ════════════════════════════════════════════════════ */
void session_start(WorkoutMode m)
{
    sessionSeconds = 0;
    warnBeepTick   = 0;

    switch (m)
    {
        case MODE_CARDIO:
            sessionLimit = CARDIO_SESSION;
            restLimit    = CARDIO_REST;
            break;
        case MODE_UPPER:
            sessionLimit = UPPER_SESSION;
            restLimit    = UPPER_REST;
            break;
        case MODE_LOWER:
            sessionLimit = LOWER_SESSION;
            restLimit    = LOWER_REST;
            break;
    }

    gymState = STATE_ACTIVE;
    timer_start();
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

    /* Force buzzer OFF before clock init — prevents startup beep */
    P1DIR |=  BUZZER_PIN;
    P1OUT &= ~BUZZER_PIN;

    BCSCTL1 = CALBC1_16MHZ;
    DCOCTL  = CALDCO_16MHZ;

    P2DIR |= BIT3 + BIT4;
    P2OUT |= BIT3 + BIT4;

    uart_init();
    buzzer_init();
    timer_init();
    LCD_init();
    LCD_clear();
    buttons_init();

    __bis_SR_register(GIE);

    lcd_show("  SMARTGYM MON  ", "  PRESS ENTRY   ");
    uart_print("=== SmartGym Monitor - Stage 5 ===\r\n");
    uart_print("Timer + Buzzer + LCD ready\r\n\r\n");

    while (1)
    {
        /* ══ 1-SECOND TICK HANDLER ══════════════════════ */
        if (timerTick)
        {
            timerTick = 0;

            /* ── Active session counting ── */
            if (gymState == STATE_ACTIVE || gymState == STATE_WARNING)
            {
                sessionSeconds++;
                uint16_t remaining = sessionLimit - sessionSeconds;

                /* Enter WARNING state when 60s left */
                if (remaining == WARN_SECONDS && gymState == STATE_ACTIVE)
                {
                    gymState     = STATE_WARNING;
                    warnBeepTick = 0;
                    buzzer_warn();
                    uart_print("WARNING | 60 seconds left\r\n\r\n");
                }

                /* During WARNING: beep every 10 seconds */
                if (gymState == STATE_WARNING)
                {
                    warnBeepTick++;
                    if (warnBeepTick >= 10)
                    {
                        warnBeepTick = 0;
                        buzzer_warn();
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

            /* ── Rest period counting ── */
            else if (gymState == STATE_REST)
            {
                if (restSeconds > 0)
                {
                    restSeconds--;
                    lcd_update_rest();
                }

                /* Rest complete */
                if (restSeconds == 0)
                {
                    gymState = STATE_IDLE;
                    buzzer_rest_end();
                    lcd_show("  REST COMPLETE ", "  PRESS ENTRY   ");
                    uart_print("REST DONE | State: IDLE\r\n\r\n");
                }
            }
        }

        /* ══ BUTTON HANDLERS ════════════════════════════ */

        /* ── EMERGENCY ── */
        if (emergencyFlag)
        {
            emergencyFlag = 0;
            timer_stop();
            gymState      = STATE_EMERGENCY;
            modeConfirmed = 0;

            lcd_show("!! EMERGENCY !!", " HELP IS COMING ");
            buzzer_emergency();
            uart_print("!! EMERGENCY !! | State: EMERGENCY\r\n\r\n");
        }

        /* ── ENTRY ── */
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
                uart_print("ENTRY | State: MODE_SELECT\r\n\r\n");
            }
            else
            {
                uart_print("ENTRY ignored | Not IDLE\r\n\r\n");
            }
        }

        /* ── EXIT ── */
        else if (exitFlag)
        {
            exitFlag = 0;

            if (gymState == STATE_MODE_SELECT ||
                gymState == STATE_ACTIVE      ||
                gymState == STATE_WARNING     ||
                gymState == STATE_REST)
            {
                timer_stop();
                gymState      = STATE_IDLE;
                modeConfirmed = 0;
                currentMode   = MODE_CARDIO;
                sessionSeconds = 0;

                buzzer_exit();
                lcd_show("  GYM IS FREE   ", "  PRESS ENTRY   ");
                uart_print("EXIT | State: IDLE\r\n\r\n");
            }
            else
            {
                uart_print("EXIT ignored | Nothing active\r\n\r\n");
            }
        }

        /* ── MODE ── */
        else if (modeFlag)
        {
            modeFlag = 0;

            if (gymState == STATE_MODE_SELECT && !modeConfirmed)
            {
                currentMode = (currentMode + 1) % 3;
                buzzer_mode();
                lcd_show_mode_select(currentMode);
            }
            else if (modeConfirmed)
            {
                uart_print("MODE ignored | Already confirmed\r\n\r\n");
            }
            else
            {
                uart_print("MODE ignored | Press Entry first\r\n\r\n");
            }
        }

        /* ── CONFIRM ── */
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
                    case MODE_UPPER:  lcd_print_padded("  UPPER  15 MIN "); break;
                    case MODE_LOWER:  lcd_print_padded("  LOWER  15 MIN "); break;
                }

                buzzer_confirm();
                DELAY_MS(1000);

                session_start(currentMode);
                lcd_update_session();

                uart_print("CONFIRM | Session started\r\n\r\n");
            }
            else if (modeConfirmed)
            {
                uart_print("CONFIRM ignored | Already locked\r\n\r\n");
            }
            else
            {
                uart_print("CONFIRM ignored | Press Entry first\r\n\r\n");
            }
        }

        __bis_SR_register(LPM0_bits);
    }
}

/* ════════════════════════════════════════════════════
 *  TIMER A0 CCR0 ISR — fires every 1 second
 * ════════════════════════════════════════════════════ */
#pragma vector = TIMER0_A0_VECTOR
__interrupt void Timer_A0_ISR(void)
{
    timerTick = 1;
    __bic_SR_register_on_exit(LPM0_bits);   /* wake main loop          */
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

/*
 * SmartGym Monitor — Stage 4
 * All 5 Buttons + I2C LCD + Buzzer
 *
 * Target : MSP430G2553
 * Clock  : 16 MHz DCO (calibrated)
 *
 * WIRING
 * ─────────────────────────────────────────────────────
 *  CP2102              MSP430
 *  ──────              ──────
 *  TXD       →         P1.1  (MSP RX)
 *  RXD       ←         P1.2  (MSP TX)
 *  GND       →         GND
 *
 *  LCD (PCF8574)       MSP430
 *  ─────────────       ──────
 *  VCC       →         3.3V / 5V
 *  GND       →         GND
 *  SDA       →         P2.3
 *  SCL       →         P2.4
 *
 *  Buzzer              MSP430
 *  ──────              ──────
 *  +  (positive)  →    P1.0
 *  -  (negative)  →    GND
 *
 *  Button      Pin    Other end
 *  ─────────   ─────  ─────────
 *  Entry       P1.3   GND  (pull-up)
 *  Exit        P1.4   3.3V (pull-down)
 *  Mode        P2.0   GND  (pull-up)
 *  Confirm     P2.1   GND  (pull-up)
 *  Emergency   P2.2   GND  (pull-up)
 *
 * BUZZER PATTERNS  (each event has a unique sound)
 * ─────────────────────────────────────────────────────
 *  Entry     : 2 short beeps        (pip pip)
 *  Exit      : 1 long beep          (beeeeep)
 *  Mode      : 1 quick click        (tick)
 *  Confirm   : 3 rising beeps       (pip pip BEEP)
 *  Emergency : rapid continuous     (BZBZBZBZBZBZ)
 * ─────────────────────────────────────────────────────
 */

#include <msp430.h>
#include "lcd_i2c.h"
#include <stdint.h>

/* ── Buzzer pin ── */
#define BUZZER      BIT0        /* P1.0 — active buzzer positive  */

/* ── Button pins ── */
#define BTN_ENTRY      BIT3
#define BTN_EXIT       BIT4
#define BTN_MODE       BIT0
#define BTN_CONFIRM    BIT1
#define BTN_EMERGENCY  BIT2

/* ── Debounce ~50ms at 16MHz ── */
#define DEBOUNCE    800000UL

/* ── Delay helpers at 16MHz ── */
#define DELAY_MS(x)   __delay_cycles((long)(16000UL * (x)))

/* ── Workout modes ── */
typedef enum { MODE_CARDIO=0, MODE_UPPER=1, MODE_LOWER=2 } WorkoutMode;

/* ── System states ── */
typedef enum {
    STATE_IDLE=0,
    STATE_MODE_SELECT,
    STATE_ACTIVE,
    STATE_EMERGENCY
} GymState;

/* ── Globals ── */
volatile GymState    gymState      = STATE_IDLE;
volatile WorkoutMode currentMode   = MODE_CARDIO;
volatile uint8_t     modeConfirmed = 0;

/* ── ISR flags ── */
volatile uint8_t entryFlag     = 0;
volatile uint8_t exitFlag      = 0;
volatile uint8_t modeFlag      = 0;
volatile uint8_t confirmFlag   = 0;
volatile uint8_t emergencyFlag = 0;

/* ════════════════════════════════════════════════════
 *  UART — 9600 baud at 16MHz
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
 *  BUZZER DRIVER
 *  Active buzzer — P1.0 HIGH = ON, LOW = OFF
 *  Each pattern is unique so user can identify event
 *  by sound alone without looking at LCD
 * ════════════════════════════════════════════════════ */
void buzzer_init(void)
{
    P1DIR |=  BUZZER;
    P1OUT &= ~BUZZER;               /* start OFF                      */
}

/* Primitive on/off helpers */
#define BUZZER_ON()   (P1OUT |=  BUZZER)
#define BUZZER_OFF()  (P1OUT &= ~BUZZER)

/*
 * ENTRY — 2 short beeps  (pip pip)
 * Feel: "welcome, you're in"
 */
void buzzer_entry(void)
{
    BUZZER_ON();  DELAY_MS(100);
    BUZZER_OFF(); DELAY_MS(80);
    BUZZER_ON();  DELAY_MS(100);
    BUZZER_OFF();
}

/*
 * EXIT — 1 long beep  (beeeeep)
 * Feel: "session ended"
 */
void buzzer_exit(void)
{
    BUZZER_ON();  DELAY_MS(600);
    BUZZER_OFF();
}

/*
 * MODE — 1 very short click  (tick)
 * Feel: "selection changed, keep going"
 * Short so it doesn't interrupt mode cycling
 */
void buzzer_mode(void)
{
    BUZZER_ON();  DELAY_MS(40);
    BUZZER_OFF();
}

/*
 * CONFIRM — 3 beeps, each longer than last  (pip pip BEEP)
 * Feel: "locked in, session starting"
 */
void buzzer_confirm(void)
{
    BUZZER_ON();  DELAY_MS(80);
    BUZZER_OFF(); DELAY_MS(60);
    BUZZER_ON();  DELAY_MS(120);
    BUZZER_OFF(); DELAY_MS(60);
    BUZZER_ON();  DELAY_MS(300);
    BUZZER_OFF();
}

/*
 * EMERGENCY — 8 rapid bursts  (BZBZBZBZBZBZBZBZ)
 * Feel: "urgent, distinct from all other sounds"
 */
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

/* ════════════════════════════════════════════════════
 *  BUTTON INIT
 * ════════════════════════════════════════════════════ */
void buttons_init(void)
{
    /* P1.3 Entry — pull-up, falling edge */
    P1DIR &= ~BTN_ENTRY;  P1REN |=  BTN_ENTRY;  P1OUT |=  BTN_ENTRY;
    P1IES |=  BTN_ENTRY;  P1IFG &= ~BTN_ENTRY;  P1IE  |=  BTN_ENTRY;

    /* P1.4 Exit — pull-down, rising edge */
    P1DIR &= ~BTN_EXIT;   P1REN |=  BTN_EXIT;   P1OUT &= ~BTN_EXIT;
    P1IES &= ~BTN_EXIT;   P1IFG &= ~BTN_EXIT;   P1IE  |=  BTN_EXIT;

    /* P2.0 Mode — pull-up, falling edge */
    P2DIR &= ~BTN_MODE;      P2REN |=  BTN_MODE;      P2OUT |=  BTN_MODE;
    P2IES |=  BTN_MODE;      P2IFG &= ~BTN_MODE;      P2IE  |=  BTN_MODE;

    /* P2.1 Confirm — pull-up, falling edge */
    P2DIR &= ~BTN_CONFIRM;   P2REN |=  BTN_CONFIRM;   P2OUT |=  BTN_CONFIRM;
    P2IES |=  BTN_CONFIRM;   P2IFG &= ~BTN_CONFIRM;   P2IE  |=  BTN_CONFIRM;

    /* P2.2 Emergency — pull-up, falling edge */
    P2DIR &= ~BTN_EMERGENCY; P2REN |=  BTN_EMERGENCY; P2OUT |=  BTN_EMERGENCY;
    P2IES |=  BTN_EMERGENCY; P2IFG &= ~BTN_EMERGENCY; P2IE  |=  BTN_EMERGENCY;
}

/* ════════════════════════════════════════════════════
 *  MAIN
 * ════════════════════════════════════════════════════ */
void main(void)
{
    WDTCTL = WDTPW | WDTHOLD;

    BCSCTL1 = CALBC1_16MHZ;
    DCOCTL  = CALDCO_16MHZ;

    /* Software I2C pins — output, idle HIGH */
    P2DIR |= BIT3 + BIT4;
    P2OUT |= BIT3 + BIT4;

    uart_init();
    buzzer_init();
    LCD_init();
    LCD_clear();
    buttons_init();

    __bis_SR_register(GIE);

    /* Startup — beep once + show welcome */
    buzzer_entry();
    lcd_show("  SMARTGYM MON  ", "  PRESS ENTRY   ");
    uart_print("=== SmartGym Monitor - Stage 4 ===\r\n");
    uart_print("Buzzer + LCD ready. State: IDLE\r\n\r\n");

    while (1)
    {
        /* ── EMERGENCY — highest priority ── */
        if (emergencyFlag)
        {
            emergencyFlag = 0;
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
                uart_print("ENTRY    | State: MODE_SELECT\r\n\r\n");
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

            if (gymState == STATE_MODE_SELECT || gymState == STATE_ACTIVE)
            {
                gymState      = STATE_IDLE;
                modeConfirmed = 0;
                currentMode   = MODE_CARDIO;

                buzzer_exit();
                lcd_show("  GYM IS FREE   ", "  PRESS ENTRY   ");
                uart_print("EXIT     | State: IDLE\r\n\r\n");
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

                uart_print("MODE     | Cycled to: ");
                switch (currentMode)
                {
                    case MODE_CARDIO: uart_print("CARDIO\r\n\r\n");     break;
                    case MODE_UPPER:  uart_print("UPPER BODY\r\n\r\n"); break;
                    case MODE_LOWER:  uart_print("LEG TRAIN\r\n\r\n");  break;
                }
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
                gymState      = STATE_ACTIVE;

                /* Show confirmed screen first */
                LCD_setCursor(0, 0); lcd_print_padded("MODE CONFIRMED! ");
                LCD_setCursor(0, 1);
                switch (currentMode)
                {
                    case MODE_CARDIO:
                        lcd_print_padded("    CARDIO      ");
                        uart_print("CONFIRM  | CARDIO | State: ACTIVE\r\n\r\n");
                        break;
                    case MODE_UPPER:
                        lcd_print_padded("  UPPER BODY    ");
                        uart_print("CONFIRM  | UPPER  | State: ACTIVE\r\n\r\n");
                        break;
                    case MODE_LOWER:
                        lcd_print_padded("  LEG TRAIN     ");
                        uart_print("CONFIRM  | LOWER  | State: ACTIVE\r\n\r\n");
                        break;
                }

                /* Confirm beep plays while screen is showing */
                buzzer_confirm();

                /* Hold 1 second then show session screen */
                DELAY_MS(1000);

                LCD_setCursor(0, 0);
                switch (currentMode)
                {
                    case MODE_CARDIO: lcd_print_padded("CARDIO  00:00   "); break;
                    case MODE_UPPER:  lcd_print_padded("UPPER   00:00   "); break;
                    case MODE_LOWER:  lcd_print_padded("LOWER   00:00   "); break;
                }
                LCD_setCursor(0, 1);
                lcd_print_padded("SESSION ACTIVE  ");
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
 *  PORT 1 ISR  —  Entry (P1.3) + Exit (P1.4)
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
 *  PORT 2 ISR  —  Mode (P2.0) + Confirm (P2.1) + Emergency (P2.2)
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

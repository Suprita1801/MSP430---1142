/*
 * SmartGym Monitor — Stage 3
 * All 5 Buttons + I2C LCD (16x2)
 *
 * Target : MSP430G2553
 * Clock  : 1 MHz DCO (calibrated)
 *
 * WIRING
 * ─────────────────────────────────────────────────────
 *  CP2102                MSP430
 *  ──────                ──────
 *  TXD         →         P1.1  (MSP RX)
 *  RXD         ←         P1.2  (MSP TX)
 *  GND         →         GND
 *
 *  LCD (PCF8574 backpack)   MSP430
 *  ──────────────────────   ──────
 *  VCC          →           3.3V (or 5V if your module needs it)
 *  GND          →           GND
 *  SDA          →           P1.6  + 4.7kΩ pull-up to 3.3V
 *  SCL          →           P1.7  + 4.7kΩ pull-up to 3.3V
 *
 *  Button       Pin    Other end
 *  ──────────   ─────  ─────────
 *  Entry        P1.3   GND  (pull-up)
 *  Exit         P1.4   3.3V (pull-down)
 *  Mode         P2.0   GND  (pull-up)
 *  Confirm      P2.1   GND  (pull-up)
 *  Emergency    P2.2   GND  (pull-up)
 *
 * LCD I2C ADDRESS
 * ─────────────────────────────────────────────────────
 *  Default : 0x27
 *  If LCD blank after init, change LCD_ADDR to 0x3F
 *
 * WHAT TO VERIFY
 * ─────────────────────────────────────────────────────
 *  Power on   → Line1: "  SMARTGYM MON "
 *               Line2: "   PRESS ENTRY "
 *
 *  Entry      → Line1: "SELECT MODE:   "
 *               Line2: "> CARDIO       "
 *
 *  Mode x1    → Line2: "> UPPER BODY   "
 *  Mode x2    → Line2: "> LEG TRAIN    "
 *  Mode x3    → Line2: "> CARDIO       "  (wraps)
 *
 *  Confirm    → Line1: "MODE CONFIRMED!"
 *               Line2: "  UPPER BODY   "
 *               (after 1s) → session screen
 *
 *  Exit       → Line1: "  GYM IS FREE  "
 *               Line2: "   PRESS ENTRY "
 *
 *  Emergency  → Line1: "!! EMERGENCY !!"
 *               Line2: " HELP IS COMING"
 *
 * UART still active — same output as Stage 2 for debugging
 * ─────────────────────────────────────────────────────
 */

#include "msp430g2553.h"
#include <stdint.h>

/* ════════════════════════════════════════════════════
 *  LCD CONFIGURATION
 * ════════════════════════════════════════════════════ */
#define LCD_ADDR    0x27        /* try 0x3F if screen stays blank */

/* PCF8574 bit positions → HD44780 pins */
#define LCD_RS      BIT0        /* P0 = RS  */
#define LCD_RW      BIT1        /* P1 = RW  (tied LOW = write)   */
#define LCD_EN      BIT2        /* P2 = EN  */
#define LCD_BL      BIT3        /* P3 = Backlight                */
#define LCD_D4      BIT4        /* P4 = D4  */
#define LCD_D5      BIT5        /* P5 = D5  */
#define LCD_D6      BIT6        /* P6 = D6  */
#define LCD_D7      BIT7        /* P7 = D7  */

/* HD44780 commands */
#define LCD_CLEAR       0x01
#define LCD_HOME        0x02
#define LCD_ENTRY_MODE  0x06    /* increment cursor, no display shift */
#define LCD_DISPLAY_ON  0x0C    /* display on, cursor off, blink off  */
#define LCD_4BIT_2LINE  0x28    /* 4-bit, 2 lines, 5x8 font           */
#define LCD_ROW2        0xC0    /* DDRAM address of row 2             */

/* ── Button pins ── */
#define BTN_ENTRY      BIT3
#define BTN_EXIT       BIT4
#define BTN_MODE       BIT0
#define BTN_CONFIRM    BIT1
#define BTN_EMERGENCY  BIT2

#define DEBOUNCE    50000UL

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

volatile uint8_t entryFlag     = 0;
volatile uint8_t exitFlag      = 0;
volatile uint8_t modeFlag      = 0;
volatile uint8_t confirmFlag   = 0;
volatile uint8_t emergencyFlag = 0;

/* ════════════════════════════════════════════════════
 *  UART (kept for debug — same as Stages 1 & 2)
 * ════════════════════════════════════════════════════ */
void uart_init(void)
{
    BCSCTL1 = CALBC1_1MHZ;
    DCOCTL  = CALDCO_1MHZ;

    P1SEL  = BIT1 + BIT2;
    P1SEL2 = BIT1 + BIT2;

    UCA0CTL1 |= UCSSEL_2;
    UCA0BR0   = 104;
    UCA0BR1   = 0;
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
 *  I2C DRIVER  (USCI_B0 — 100 kHz)
 * ════════════════════════════════════════════════════ */
void i2c_init(void)
{
    /* Route P1.6 = SDA, P1.7 = SCL to USCI_B0 */
    P1SEL  |= BIT6 + BIT7;
    P1SEL2 |= BIT6 + BIT7;

    UCB0CTL1 |= UCSWRST;               /* hold in reset              */
    UCB0CTL0  = UCMST | UCMODE_3 | UCSYNC; /* I2C master             */
    UCB0CTL1  = UCSSEL_2 | UCSWRST;    /* SMCLK, still in reset      */
    UCB0BR0   = 10;                    /* 1MHz / 10 = 100 kHz        */
    UCB0BR1   = 0;
    UCB0I2CSA = LCD_ADDR;              /* slave address              */
    UCB0CTL1 &= ~UCSWRST;             /* release from reset          */
}

void i2c_start(void)
{
    UCB0CTL1 |= UCTR | UCTXSTT;        /* TX mode + START            */
    while (UCB0CTL1 & UCTXSTT);        /* wait until START sent      */
}

void i2c_write_byte(uint8_t data)
{
    UCB0TXBUF = data;
    while (!(IFG2 & UCB0TXIFG));       /* wait for TX complete       */
}

void i2c_stop(void)
{
    UCB0CTL1 |= UCTXSTP;               /* send STOP                  */
    while (UCB0CTL1 & UCTXSTP);        /* wait until STOP sent       */
}

/* ════════════════════════════════════════════════════
 *  LCD DRIVER  (PCF8574 → HD44780, 4-bit mode)
 * ════════════════════════════════════════════════════ */

/* Send one byte to PCF8574 (all 8 pins at once) */
void lcd_pcf_write(uint8_t data)
{
    i2c_start();
    i2c_write_byte(data | LCD_BL);     /* always keep backlight ON   */
    i2c_stop();
}

/* Pulse EN pin to latch nibble into HD44780 */
void lcd_pulse_en(uint8_t data)
{
    lcd_pcf_write(data | LCD_EN);      /* EN HIGH                    */
    __delay_cycles(500);
    lcd_pcf_write(data & ~LCD_EN);     /* EN LOW  — latches nibble   */
    __delay_cycles(500);
}

/*
 * Send one full byte (command or data) as two 4-bit nibbles
 * mode: 0 = command (RS LOW), 1 = data/character (RS HIGH)
 */
void lcd_send_byte(uint8_t byte, uint8_t mode)
{
    uint8_t high = byte & 0xF0;                 /* upper nibble       */
    uint8_t low  = (byte << 4) & 0xF0;          /* lower nibble       */

    uint8_t rs = mode ? LCD_RS : 0;

    lcd_pulse_en(high | rs);
    lcd_pulse_en(low  | rs);

    /* Commands need extra settling time */
    if (!mode)
    {
        if (byte == LCD_CLEAR || byte == LCD_HOME)
            __delay_cycles(1600);               /* 1.6 ms             */
        else
            __delay_cycles(400);                /* 40 µs              */
    }
}

#define lcd_cmd(c)   lcd_send_byte(c, 0)
#define lcd_char(c)  lcd_send_byte(c, 1)

void lcd_print(const char *str)
{
    while (*str) lcd_char(*str++);
}

/* Position cursor: row 0 or 1, col 0-15 */
void lcd_set_cursor(uint8_t row, uint8_t col)
{
    uint8_t addr = (row == 0) ? (0x80 + col) : (0xC0 + col);
    lcd_cmd(addr);
}

/* Print a string padded with spaces to fill 16 columns */
void lcd_print_padded(const char *str)
{
    uint8_t i = 0;
    while (str[i] && i < 16) { lcd_char(str[i]); i++; }
    while (i < 16)            { lcd_char(' ');    i++; }
}

/*
 * HD44780 4-bit initialisation sequence
 * Follows the exact power-on procedure from the datasheet
 */
void lcd_init(void)
{
    __delay_cycles(15000);              /* >15 ms after VCC rises     */

    /* Special 3-step 8-bit wake-up (send 0x30 three times) */
    lcd_pcf_write(0x30 | LCD_EN); __delay_cycles(500);
    lcd_pcf_write(0x30);          __delay_cycles(4100);

    lcd_pcf_write(0x30 | LCD_EN); __delay_cycles(500);
    lcd_pcf_write(0x30);          __delay_cycles(200);

    lcd_pcf_write(0x30 | LCD_EN); __delay_cycles(500);
    lcd_pcf_write(0x30);          __delay_cycles(200);

    /* Switch to 4-bit mode */
    lcd_pcf_write(0x20 | LCD_EN); __delay_cycles(500);
    lcd_pcf_write(0x20);          __delay_cycles(200);

    /* Now use proper 4-bit commands */
    lcd_cmd(LCD_4BIT_2LINE);       /* 4-bit, 2 lines, 5x8           */
    lcd_cmd(0x08);                 /* display OFF                    */
    lcd_cmd(LCD_CLEAR);            /* clear display                  */
    lcd_cmd(LCD_ENTRY_MODE);       /* entry mode                     */
    lcd_cmd(LCD_DISPLAY_ON);       /* display ON, cursor OFF         */
}

/* ── Helper: show a 2-line message ── */
void lcd_show(const char *line1, const char *line2)
{
    lcd_set_cursor(0, 0); lcd_print_padded(line1);
    lcd_set_cursor(1, 0); lcd_print_padded(line2);
}

/* ── Helper: mode name for LCD line 2 ── */
void lcd_show_mode_select(WorkoutMode m)
{
    lcd_set_cursor(0, 0); lcd_print_padded("SELECT MODE:    ");
    lcd_set_cursor(1, 0);
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
void main(void)
{
    WDTCTL = WDTPW + WDTHOLD;

    uart_init();
    i2c_init();
    lcd_init();
    buttons_init();

    __bis_SR_register(GIE);

    /* ── Startup screen ── */
    lcd_show("  SMARTGYM MON  ", "  PRESS ENTRY   ");
    uart_print("=== SmartGym Monitor - Stage 3 ===\r\n");
    uart_print("LCD initialised. State: IDLE\r\n\r\n");

    while (1)
    {
        /* ── EMERGENCY — highest priority ── */
        if (emergencyFlag)
        {
            emergencyFlag = 0;
            gymState      = STATE_EMERGENCY;
            modeConfirmed = 0;

            lcd_show("!! EMERGENCY !!", " HELP IS COMING ");
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

                /* Show confirmed screen */
                lcd_set_cursor(0, 0); lcd_print_padded("MODE CONFIRMED! ");
                switch (currentMode)
                {
                    case MODE_CARDIO:
                        lcd_set_cursor(1, 0); lcd_print_padded("   CARDIO       ");
                        uart_print("CONFIRM  | CARDIO locked | State: ACTIVE\r\n\r\n");
                        break;
                    case MODE_UPPER:
                        lcd_set_cursor(1, 0); lcd_print_padded("  UPPER BODY    ");
                        uart_print("CONFIRM  | UPPER locked  | State: ACTIVE\r\n\r\n");
                        break;
                    case MODE_LOWER:
                        lcd_set_cursor(1, 0); lcd_print_padded("  LEG TRAIN     ");
                        uart_print("CONFIRM  | LOWER locked  | State: ACTIVE\r\n\r\n");
                        break;
                }

                /* Hold confirmed screen 1 second then show session screen */
                __delay_cycles(1000000);

                lcd_set_cursor(0, 0);
                switch (currentMode)
                {
                    case MODE_CARDIO: lcd_print_padded("CARDIO  00:00   "); break;
                    case MODE_UPPER:  lcd_print_padded("UPPER   00:00   "); break;
                    case MODE_LOWER:  lcd_print_padded("LOWER   00:00   "); break;
                }
                lcd_set_cursor(1, 0); lcd_print_padded("SESSION ACTIVE  ");
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
 *  PORT 1 ISR  —  Entry + Exit
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
 *  PORT 2 ISR  —  Mode + Confirm + Emergency
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

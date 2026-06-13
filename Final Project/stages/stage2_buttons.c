/*
 * SmartGym Monitor — Stage 2
 * All 5 Buttons  (P1.3, P1.4, P2.0, P2.1, P2.2)
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
 *  Button       Pin    Other end   Edge
 *  ──────────   ─────  ─────────   ────────────
 *  Entry        P1.3   GND         Falling (pull-up)
 *  Exit         P1.4   3.3V        Rising  (pull-down)
 *  Mode         P2.0   GND         Falling (pull-up)
 *  Confirm      P2.1   GND         Falling (pull-up)
 *  Emergency    P2.2   GND         Falling (pull-up)
 *
 * WHAT TO VERIFY
 * ─────────────────────────────────────────────────────
 *  1. Press Entry       → "ENTRY    | State: IN_GYM"
 *  2. Press Mode        → "MODE     | Mode: CARDIO(0)"
 *  3. Press Mode again  → "MODE     | Mode: UPPER(1)"
 *  4. Press Mode again  → "MODE     | Mode: LOWER(2)"
 *  5. Press Mode again  → "MODE     | Mode: CARDIO(0)"  (wraps)
 *  6. Press Confirm     → "CONFIRM  | Mode locked: CARDIO(0)"
 *  7. Press Confirm again → "CONFIRM ignored | Mode already locked"
 *  8. Press Mode after confirm → "MODE ignored | Already confirmed"
 *  9. Press Exit        → "EXIT     | State: IDLE  | Mode reset"
 * 10. Press Emergency   → "EMERGENCY!! | All states overridden"
 * ─────────────────────────────────────────────────────
 */

#include "msp430g2553.h"
#include <stdint.h>

/* ── Button pins ── */
#define BTN_ENTRY      BIT3     /* P1.3 — pull-up,   active LOW  */
#define BTN_EXIT       BIT4     /* P1.4 — pull-down, active HIGH */
#define BTN_MODE       BIT0     /* P2.0 — pull-up,   active LOW  */
#define BTN_CONFIRM    BIT1     /* P2.1 — pull-up,   active LOW  */
#define BTN_EMERGENCY  BIT2     /* P2.2 — pull-up,   active LOW  */

/* ── Debounce (~50 ms at 1 MHz) ── */
#define DEBOUNCE    50000UL

/* ── Workout modes ── */
typedef enum {
    MODE_CARDIO = 0,
    MODE_UPPER  = 1,
    MODE_LOWER  = 2
} WorkoutMode;

/* ── System states ── */
typedef enum {
    STATE_IDLE        = 0,
    STATE_MODE_SELECT,       /* entered gym, choosing mode    */
    STATE_ACTIVE,            /* mode confirmed, session on    */
    STATE_EMERGENCY
} GymState;

/* ── Global variables ── */
volatile GymState   gymState      = STATE_IDLE;
volatile WorkoutMode currentMode  = MODE_CARDIO;
volatile uint8_t    modeConfirmed = 0;

/* ── ISR flags ── */
volatile uint8_t entryFlag     = 0;
volatile uint8_t exitFlag      = 0;
volatile uint8_t modeFlag      = 0;
volatile uint8_t confirmFlag   = 0;
volatile uint8_t emergencyFlag = 0;

/* ══════════════════════════════════════════════════════
 *  UART  — same as your reference code
 * ══════════════════════════════════════════════════════ */
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
    while (*str)
        uart_send_char(*str++);
}

/* ── Print mode name helper ── */
void uart_print_mode(WorkoutMode m)
{
    switch (m)
    {
        case MODE_CARDIO: uart_print("CARDIO(0)"); break;
        case MODE_UPPER:  uart_print("UPPER(1)");  break;
        case MODE_LOWER:  uart_print("LOWER(2)");  break;
    }
}

/* ══════════════════════════════════════════════════════
 *  BUTTON INIT
 * ══════════════════════════════════════════════════════ */
void buttons_init(void)
{
    /* ── PORT 1 ── */

    /* P1.3 Entry — pull-up, falling edge */
    P1DIR  &= ~BTN_ENTRY;
    P1REN  |=  BTN_ENTRY;
    P1OUT  |=  BTN_ENTRY;
    P1IES  |=  BTN_ENTRY;
    P1IFG  &= ~BTN_ENTRY;
    P1IE   |=  BTN_ENTRY;

    /* P1.4 Exit — pull-down, rising edge */
    P1DIR  &= ~BTN_EXIT;
    P1REN  |=  BTN_EXIT;
    P1OUT  &= ~BTN_EXIT;
    P1IES  &= ~BTN_EXIT;
    P1IFG  &= ~BTN_EXIT;
    P1IE   |=  BTN_EXIT;

    /* ── PORT 2 ── */

    /* P2.0 Mode — pull-up, falling edge */
    P2DIR  &= ~BTN_MODE;
    P2REN  |=  BTN_MODE;
    P2OUT  |=  BTN_MODE;
    P2IES  |=  BTN_MODE;
    P2IFG  &= ~BTN_MODE;
    P2IE   |=  BTN_MODE;

    /* P2.1 Confirm — pull-up, falling edge */
    P2DIR  &= ~BTN_CONFIRM;
    P2REN  |=  BTN_CONFIRM;
    P2OUT  |=  BTN_CONFIRM;
    P2IES  |=  BTN_CONFIRM;
    P2IFG  &= ~BTN_CONFIRM;
    P2IE   |=  BTN_CONFIRM;

    /* P2.2 Emergency — pull-up, falling edge */
    P2DIR  &= ~BTN_EMERGENCY;
    P2REN  |=  BTN_EMERGENCY;
    P2OUT  |=  BTN_EMERGENCY;
    P2IES  |=  BTN_EMERGENCY;
    P2IFG  &= ~BTN_EMERGENCY;
    P2IE   |=  BTN_EMERGENCY;
}

/* ══════════════════════════════════════════════════════
 *  MAIN
 * ══════════════════════════════════════════════════════ */
void main(void)
{
    WDTCTL = WDTPW + WDTHOLD;

    uart_init();
    buttons_init();

    __bis_SR_register(GIE);

    uart_print("=== SmartGym Monitor - Stage 2 ===\r\n");
    uart_print("All 5 buttons active\r\n");
    uart_print("State: IDLE - press Entry to begin\r\n\r\n");

    while (1)
    {
        /* ── EMERGENCY — highest priority, checked first ── */
        if (emergencyFlag)
        {
            emergencyFlag = 0;
            gymState      = STATE_EMERGENCY;
            modeConfirmed = 0;
            uart_print("!! EMERGENCY !! | All states overridden\r\n");
            uart_print("State: EMERGENCY\r\n\r\n");
        }

        /* ── ENTRY ── */
        else if (entryFlag)
        {
            entryFlag = 0;

            if (gymState == STATE_IDLE)
            {
                gymState      = STATE_MODE_SELECT;
                modeConfirmed = 0;
                currentMode   = MODE_CARDIO;        /* reset to default */
                uart_print("ENTRY    | State: MODE_SELECT\r\n");
                uart_print("         | Select mode then press Confirm\r\n");
                uart_print("         | Current mode: CARDIO(0)\r\n\r\n");
            }
            else
            {
                uart_print("ENTRY ignored | Not in IDLE state\r\n\r\n");
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
                uart_print("EXIT     | State: IDLE | Mode reset\r\n\r\n");
            }
            else
            {
                uart_print("EXIT ignored | Nothing active to exit\r\n\r\n");
            }
        }

        /* ── MODE ── */
        else if (modeFlag)
        {
            modeFlag = 0;

            if (gymState == STATE_MODE_SELECT && !modeConfirmed)
            {
                /* Cycle: 0 → 1 → 2 → 0 */
                currentMode = (currentMode + 1) % 3;
                uart_print("MODE     | Mode: ");
                uart_print_mode(currentMode);
                uart_print("\r\n\r\n");
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
                uart_print("CONFIRM  | Mode locked: ");
                uart_print_mode(currentMode);
                uart_print("\r\n");
                uart_print("         | State: ACTIVE\r\n\r\n");
            }
            else if (modeConfirmed)
            {
                uart_print("CONFIRM ignored | Mode already locked\r\n\r\n");
            }
            else
            {
                uart_print("CONFIRM ignored | Press Entry first\r\n\r\n");
            }
        }

        __bis_SR_register(LPM0_bits);   /* sleep until next button press */
    }
}

/* ══════════════════════════════════════════════════════
 *  PORT 1 ISR  —  Entry (P1.3)  +  Exit (P1.4)
 * ══════════════════════════════════════════════════════ */
#pragma vector = PORT1_VECTOR
__interrupt void Port1_ISR(void)
{
    __delay_cycles(DEBOUNCE);

    if (P1IFG & BTN_ENTRY)
    {
        if (!(P1IN & BTN_ENTRY))            /* still LOW = real press    */
        {
            entryFlag = 1;
            __bic_SR_register_on_exit(LPM0_bits);
        }
        P1IFG &= ~BTN_ENTRY;
    }

    if (P1IFG & BTN_EXIT)
    {
        if (P1IN & BTN_EXIT)                /* still HIGH = real press   */
        {
            exitFlag = 1;
            __bic_SR_register_on_exit(LPM0_bits);
        }
        P1IFG &= ~BTN_EXIT;
    }
}

/* ══════════════════════════════════════════════════════
 *  PORT 2 ISR  —  Mode (P2.0) + Confirm (P2.1) + Emergency (P2.2)
 * ══════════════════════════════════════════════════════ */
#pragma vector = PORT2_VECTOR
__interrupt void Port2_ISR(void)
{
    __delay_cycles(DEBOUNCE);

    if (P2IFG & BTN_MODE)
    {
        if (!(P2IN & BTN_MODE))
        {
            modeFlag = 1;
            __bic_SR_register_on_exit(LPM0_bits);
        }
        P2IFG &= ~BTN_MODE;
    }

    if (P2IFG & BTN_CONFIRM)
    {
        if (!(P2IN & BTN_CONFIRM))
        {
            confirmFlag = 1;
            __bic_SR_register_on_exit(LPM0_bits);
        }
        P2IFG &= ~BTN_CONFIRM;
    }

    if (P2IFG & BTN_EMERGENCY)
    {
        if (!(P2IN & BTN_EMERGENCY))
        {
            emergencyFlag = 1;
            __bic_SR_register_on_exit(LPM0_bits);
        }
        P2IFG &= ~BTN_EMERGENCY;
    }
}

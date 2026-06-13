/*
 * SmartGym Monitor — Stage 1
 * Entry & Exit Buttons (P1.3, P1.4)
 * UART via CP2102  —  P1.2 TXD → CP2102 RXD
 *
 * Target : MSP430G2553
 * Clock  : 1 MHz DCO (calibrated)
 *
 * WIRING
 * ─────────────────────────────────────────
 *  P1.1  →  CP2102 TXD  (MSP RX)
 *  P1.2  →  CP2102 RXD  (MSP TX)
 *  GND   →  CP2102 GND  (common ground)
 *
 *  P1.3  →  Entry button  →  GND   (internal pull-up)
 *  P1.4  →  Exit  button  →  3.3V  (internal pull-down)
 *
 * SERIAL MONITOR SETTINGS
 * ─────────────────────────────────────────
 *  Baud: 9600 | 8N1 | No flow control
 *
 * EXPECTED OUTPUT
 * ─────────────────────────────────────────
 *  Press Entry  →  "ENTRY PRESSED  | State: IN_GYM"
 *  Press Exit   →  "EXIT  PRESSED  | State: IDLE"
 *  Press Entry again while IN_GYM  →  "ENTRY ignored  | Already IN_GYM"
 *  Press Exit  while IDLE          →  "EXIT  ignored  | Already IDLE"
 * ─────────────────────────────────────────
 */

#include "msp430g2553.h"
#include <stdint.h>

/* ── Button pins ── */
#define BTN_ENTRY   BIT3        /* P1.3 — pull-up,   active LOW  */
#define BTN_EXIT    BIT4        /* P1.4 — pull-down, active HIGH */

/* ── Debounce (~50 ms at 1 MHz) ── */
#define DEBOUNCE    50000UL

/* ── System states ── */
typedef enum {
    STATE_IDLE = 0,
    STATE_IN_GYM
} GymState;

volatile GymState gymState = STATE_IDLE;

/* ISR sets these flags; main loop reads and clears them */
volatile uint8_t entryFlag = 0;
volatile uint8_t exitFlag  = 0;

/* ══════════════════════════════════════════════════════
 *  UART INIT  — based on your reference code exactly
 * ══════════════════════════════════════════════════════ */
void uart_init(void)
{
    BCSCTL1 = CALBC1_1MHZ;             /* calibrated 1 MHz range        */
    DCOCTL  = CALDCO_1MHZ;             /* calibrated DCO step + mod      */

    P1SEL  = BIT1 + BIT2;              /* P1.1 = RXD, P1.2 = TXD        */
    P1SEL2 = BIT1 + BIT2;

    UCA0CTL1 |= UCSSEL_2;              /* clock source = SMCLK (1 MHz)  */
    UCA0BR0   = 104;                   /* 1 MHz / 9600 = 104             */
    UCA0BR1   = 0;
    UCA0CTL1 &= ~UCSWRST;             /* release USCI state machine      */
}

/* ── Send one character ── */
void uart_send_char(char c)
{
    while (!(IFG2 & UCA0TXIFG));       /* wait for TX buffer empty       */
    UCA0TXBUF = c;
}

/* ── Send a null-terminated string ── */
void uart_print(const char *str)
{
    while (*str)
        uart_send_char(*str++);
}

/* ══════════════════════════════════════════════════════
 *  BUTTON INIT
 * ══════════════════════════════════════════════════════ */
void buttons_init(void)
{
    /* P1.3 — Entry
     *  Internal pull-up  → pin HIGH at rest
     *  Falling edge IRQ  → fires on press (HIGH → LOW)
     */
    P1DIR  &= ~BTN_ENTRY;
    P1REN  |=  BTN_ENTRY;              /* enable resistor                */
    P1OUT  |=  BTN_ENTRY;              /* pull UP                        */
    P1IES  |=  BTN_ENTRY;              /* falling edge trigger           */
    P1IFG  &= ~BTN_ENTRY;              /* clear any stale flag           */
    P1IE   |=  BTN_ENTRY;              /* enable interrupt               */

    /* P1.4 — Exit
     *  Internal pull-down → pin LOW at rest
     *  Rising edge IRQ    → fires on press (LOW → HIGH)
     */
    P1DIR  &= ~BTN_EXIT;
    P1REN  |=  BTN_EXIT;
    P1OUT  &= ~BTN_EXIT;               /* pull DOWN                      */
    P1IES  &= ~BTN_EXIT;               /* rising edge trigger            */
    P1IFG  &= ~BTN_EXIT;
    P1IE   |=  BTN_EXIT;
}

/* ══════════════════════════════════════════════════════
 *  MAIN
 * ══════════════════════════════════════════════════════ */
void main(void)
{
    WDTCTL = WDTPW + WDTHOLD;          /* stop watchdog                  */

    uart_init();
    buttons_init();

    __bis_SR_register(GIE);            /* global interrupts ON           */

    uart_print("=== SmartGym Monitor - Stage 1 ===\r\n");
    uart_print("State: IDLE - waiting for entry...\r\n\r\n");

    while (1)
    {
        /* ── Entry button pressed ── */
        if (entryFlag)
        {
            entryFlag = 0;

            if (gymState == STATE_IDLE)
            {
                gymState = STATE_IN_GYM;
                uart_print("ENTRY PRESSED  | State: IN_GYM\r\n");
            }
            else
            {
                uart_print("ENTRY ignored  | Already IN_GYM\r\n");
            }
        }

        /* ── Exit button pressed ── */
        if (exitFlag)
        {
            exitFlag = 0;

            if (gymState == STATE_IN_GYM)
            {
                gymState = STATE_IDLE;
                uart_print("EXIT  PRESSED  | State: IDLE\r\n");
            }
            else
            {
                uart_print("EXIT  ignored  | Already IDLE\r\n");
            }
        }

        /* Sleep until next interrupt */
        __bis_SR_register(LPM0_bits);
    }
}

/* ══════════════════════════════════════════════════════
 *  PORT 1 ISR  —  Entry (P1.3) and Exit (P1.4)
 * ══════════════════════════════════════════════════════ */
#pragma vector = PORT1_VECTOR
__interrupt void Port1_ISR(void)
{
    __delay_cycles(DEBOUNCE);          /* debounce delay                 */

    /* ── Entry button ── */
    if (P1IFG & BTN_ENTRY)
    {
        if (!(P1IN & BTN_ENTRY))       /* confirm pin still LOW          */
        {
            entryFlag = 1;
            __bic_SR_register_on_exit(LPM0_bits);   /* wake main loop   */
        }
        P1IFG &= ~BTN_ENTRY;
    }

    /* ── Exit button ── */
    if (P1IFG & BTN_EXIT)
    {
        if (P1IN & BTN_EXIT)           /* confirm pin still HIGH         */
        {
            exitFlag = 1;
            __bic_SR_register_on_exit(LPM0_bits);   /* wake main loop   */
        }
        P1IFG &= ~BTN_EXIT;
    }
}

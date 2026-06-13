/*
 * SmartGym — HM-10 Bluetooth BLE Test
 * Software UART on P2.5 (TX only) → HM-10 RXD
 * Hardware UART on P1.1/P1.2      → CP2102 (debug)
 *
 * Target : MSP430G2553
 * Clock  : 16 MHz DCO (calibrated)
 *
 * WIRING
 * ─────────────────────────────────────────────────────
 *  HM-10               MSP430
 *  VCC       →         3.3V   (NOT 5V)
 *  GND       →         GND
 *  RXD       →         P2.5   (software UART TX)
 *  TXD       →         P2.6   (leave unconnected for now)
 *
 *  CP2102              MSP430
 *  TXD       →         P1.1   (debug RX)
 *  RXD       ←         P1.2   (debug TX)
 *  GND       →         GND
 *
 * HOW TO TEST
 * ─────────────────────────────────────────────────────
 *  1. Flash this code
 *  2. Open LightBlue app on phone
 *  3. Scan for BLE devices → find "HMSoft" or "HM-10"
 *  4. Connect to it
 *  5. Go to the RX characteristic (UUID FFE1)
 *  6. Enable notifications / Listen
 *  7. You should see messages every 2 seconds:
 *       "SmartGym BLE Test 1"
 *       "SmartGym BLE Test 2"
 *       "SmartGym BLE Test 3"
 *       ...
 *  8. CP2102 serial monitor also shows same messages
 *
 * SOFTWARE UART EXPLANATION
 * ─────────────────────────────────────────────────────
 *  Bit-bang UART at 9600 baud on P2.5
 *  Bit period = 1/9600 = 104.16 µs
 *  At 16MHz: 104.16µs × 16000 cycles/ms = 1666 cycles per bit
 *
 *  Frame: 1 start bit (LOW) + 8 data bits + 1 stop bit (HIGH)
 *  LSB first (standard UART)
 * ─────────────────────────────────────────────────────
 */

#include <msp430.h>
#include <stdint.h>

/* ── Software UART pin (HM-10) ── */
#define BLE_TX_PIN      BIT5            /* P2.5 → HM-10 RXD           */
#define BLE_TX_OUT      P2OUT
#define BLE_TX_DIR      P2DIR

/* ── Bit timing at 16MHz, 9600 baud ── */
/* 16000000 / 9600 = 1666.67 cycles per bit */
#define BIT_CYCLES      1666

/* ── Delay helpers ── */
#define DELAY_MS(x)     __delay_cycles((long)(16000UL * (x)))

/* ════════════════════════════════════════════════════
 *  HARDWARE UART — CP2102 debug (P1.1/P1.2)
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
 *  SOFTWARE UART — HM-10 BLE (P2.5)
 *
 *  Sends one byte using bit-bang:
 *  1. Pull LOW  = start bit  (1 bit period)
 *  2. Send bits LSB first    (8 bit periods)
 *  3. Pull HIGH = stop bit   (1 bit period)
 * ════════════════════════════════════════════════════ */
void ble_init(void)
{
    BLE_TX_DIR |= BLE_TX_PIN;
    BLE_TX_OUT |= BLE_TX_PIN;      /* idle HIGH (UART idle state)    */
}

void ble_send_byte(uint8_t byte)
{
    uint8_t i;

    /* Start bit — LOW */
    BLE_TX_OUT &= ~BLE_TX_PIN;
    __delay_cycles(BIT_CYCLES);

    /* 8 data bits — LSB first */
    for (i = 0; i < 8; i++)
    {
        if (byte & 0x01)
            BLE_TX_OUT |=  BLE_TX_PIN;  /* bit = 1 → HIGH             */
        else
            BLE_TX_OUT &= ~BLE_TX_PIN;  /* bit = 0 → LOW              */

        __delay_cycles(BIT_CYCLES);
        byte >>= 1;
    }

    /* Stop bit — HIGH */
    BLE_TX_OUT |= BLE_TX_PIN;
    __delay_cycles(BIT_CYCLES);
}

void ble_print(const char *str)
{
    while (*str) ble_send_byte(*str++);
}

/* Send string + carriage return + newline */
void ble_println(const char *str)
{
    ble_print(str);
    ble_send_byte('\r');
    ble_send_byte('\n');
}

/* ════════════════════════════════════════════════════
 *  PRINT UNSIGNED INT (for counter)
 * ════════════════════════════════════════════════════ */
void ble_print_uint(uint16_t n)
{
    char buf[6];
    uint8_t i = 0;
    if (n == 0) { ble_send_byte('0'); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i--) ble_send_byte(buf[i]);
}

void uart_print_uint(uint16_t n)
{
    char buf[6];
    uint8_t i = 0;
    if (n == 0) { uart_send_char('0'); return; }
    while (n > 0) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i--) uart_send_char(buf[i]);
}

/* ════════════════════════════════════════════════════
 *  MAIN
 * ════════════════════════════════════════════════════ */
void main(void)
{
    WDTCTL = WDTPW | WDTHOLD;

    BCSCTL1 = CALBC1_16MHZ;
    DCOCTL  = CALDCO_16MHZ;

    uart_init();
    ble_init();

    /* Startup message on CP2102 */
    uart_print("=== HM-10 BLE Test ===\r\n");
    uart_print("Software UART on P2.5 at 9600 baud\r\n");
    uart_print("Connect LightBlue → HMSoft → FFE1\r\n\r\n");

    /*
     * Send AT command first to verify HM-10 is alive.
     * HM-10 responds with "OK" — visible on LightBlue.
     * Wait 500ms after power on before sending AT.
     */
    DELAY_MS(500);
    ble_print("AT");                    /* HM-10 should respond "OK"  */
    uart_print("Sent AT command to HM-10\r\n");
    DELAY_MS(500);

    /* ── Send test messages in a loop ── */
    uint16_t count = 1;

    while (1)
    {
        /* Send to HM-10 → LightBlue */
        ble_print("SmartGym BLE Test ");
        ble_print_uint(count);
        ble_println("");

        /* Mirror to CP2102 serial monitor */
        uart_print("BLE sent: SmartGym BLE Test ");
        uart_print_uint(count);
        uart_print("\r\n");

        count++;

        /* Wait 2 seconds between messages */
        DELAY_MS(2000);
    }
}

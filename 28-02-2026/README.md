# MSP430 GPIO — Lab Session 1

**28-02-2026** · Texas Instruments MSP430 · Microprocessor Applications · Hands-on Training

---

| | | |
|---|---|---|
| **microcontroller** | **IDE** | **projects** |
| MSP430G2553 | Code Composer Studio | 9 completed |

---

## 📋 Project Log

| # | Type | Description | Pins |
|---|------|-------------|------|
| 01 | Output | Hello — P1.0 LED1 basic output check | P1.0 |
| 02 | Output | LED — Alternating blink with delay cycles | P1.0 P1.6 |
| 03 | Output | Digital output — P1.4 enable with while loop delay | P1.0 P1.4 P1.6 |
| 04 | Output | Digital output — Multi-port output with P2.3 | P1.0 P1.4 P2.3 |
| 05 | Input | Floating input — if-else LED control via P1.5 | P1.5 → P1.0 P1.6 |
| 06 | Pull-up/dn | Floating fix — P1REN resistor prevents float on P1.1 | P1.1 → P1.0 P1.6 |
| 07 | Multi if-else | Multi-input — P2.1 P2.2 with enabled resistors | P2.1 P2.2 → P1.0 P1.6 |
| 08 | Switch-case | Switch case — replaces if-else for cleaner structure | P2.1 P2.2 → P1.0 P1.6 |
| 09 | Mixed | Mixed config — P2.1 pull-down, P2.2 floating | P2.1 P2.2 → P1.0 P1.6 |

---

## ⚙️ Key Concepts Covered

`P1DIR / P2DIR` · `P1OUT / P2OUT` · `P1IN / P2IN` · `P1REN pull-up/down` · `Bitwise OR |=` · `Bitwise AND &= ~` · `XOR toggle ^=` · `__delay_cycles()` · `Switch-case` · `Floating input fix`

---

*MSP430G2553 · GPIO fundamentals · TI LaunchPad ready*

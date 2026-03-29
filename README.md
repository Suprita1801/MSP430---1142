# MSP430 — 1142 · Microprocessor Application

> Exchange Program · HungKuang University · Taiwan

---

## About

During my exchange program at **HungKuang University** in Taiwan, I chose *Microprocessor Application* as one of my elective courses. The objective was to build practical skills in implementing the **Texas Instruments MSP430** microcontroller — from basic GPIO control to structured digital input handling.

| | |
|---|---|
| **University** | HungKuang University |
| **Location** | Taiwan |
| **Course Type** | Elective |
| **Microcontroller** | Texas Instruments MSP430G2553 |
| **IDE** | Code Composer Studio (CCS) |
| **Focus** | Hands-on practical implementation |

---

## What This Repo Covers

### GPIO Output Control
LED blinking, multi-port output, delay cycles, XOR toggle using `P1DIR` and `P1OUT` registers.

### Digital Input Handling
Floating input prevention, pull-up/pull-down resistors via `P1REN` and input reading via `P1IN`.

### Bitwise Operations
Practical use of OR `|=`, AND `&= ~`, and XOR `^=` for setting, clearing, and toggling bits.

### Control Flow
`if-else` and `switch-case` structures for multi-input decision making with jumpers and onboard buttons.

---

## Key Concepts

`P1DIR` `P2DIR` `P1OUT` `P2OUT` `P1IN` `P2IN` `P1REN`
`Pull-up resistor` `Pull-down resistor` `Bitwise OR` `Bitwise AND` `XOR toggle`
`__delay_cycles()` `switch-case` `Floating input fix`

---

## Repository Structure

```
MSP430---1142/
│
├── 28-02-2026/          ← Lab session 1 · GPIO basics
│   ├── hello.c
│   ├── led.c
│   ├── digital_output.c
│   ├── digital_input.c
│   └── ...
│
└── README.md
```

> **Note:** Each folder contains only `.c` reference source files.
> Full CCS project folders (linker scripts, includes, debug configs) are **not** committed.
> These are standalone code references only.

---

## Lab Sessions

| Date | Session | Projects |
|------|---------|----------|
| 28-02-2026 | GPIO Basics | 9 projects — output, input, switch-case |

---

*MSP430 · Texas Instruments · HungKuang University · Taiwan Exchange Program*

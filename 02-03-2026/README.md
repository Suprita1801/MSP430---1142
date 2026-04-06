# Lab Session — 02-03-2026
## Digital Input Handling · Pull-up & Pull-down · XOR Toggle · Multi-Input Control

> MSP430G2553 · Code Composer Studio · HungKuang University

---

## Overview

This session builds on GPIO output basics and introduces **digital input handling**
using pull-up and pull-down resistors. Concepts progress from simple input reading
to multi-input decision making with alternating LED control.

---

## Programs

### 01 · Pull-down Intro (`01_pulldown_intro`)
- **Input:** P2.0 (pull-down resistor enabled)
- **Output:** None
- **Concept:** Introduction to pull-up vs pull-down resistors.
  Demonstrates floating input prevention. No output operation performed.

---

### 02 · Pull-down → LED (`02_pulldown_led`)
- **Input:** P2.0 (pull-down)
- **Output:** P1.0 (LED)
- **Concept:** Reads P2.0 state and verifies pull-down functionality
  through LED response on P1.0.

---

### 03 · Pull-up → LED (`03_pullup_led`)
- **Input:** P2.0 (pull-up)
- **Output:** P1.0 / P1.6 (LED)
- **Concept:** Reads P2.0 state with pull-up resistor.
  Verifies functionality through LED on P1.0 and optionally P1.6.

---

### 04 · Digital Input + XOR — P1.3 (`04_digitalinput_xor_p13`)
- **Input:** P1.3 (pull-up)
- **Output:** P1.0 + P1.6 (both LEDs toggle simultaneously)
- **Logic:**
  - Condition TRUE (button not pressed) → 0.2 sec delay
  - Condition FALSE (button pressed) → 2 sec delay
- **Concept:** XOR toggle `P1OUT ^= (BIT0 + BIT6)` at end of if-else.

---

### 05 · Digital Input + XOR — P2.0 (`05_digitalinput_xor_p20`)
- **Input:** P2.0 (pull-down)
- **Output:** P1.0 (LED toggles)
- **Logic:**
  - Condition TRUE (button pressed) → 0.1 sec delay
  - Condition FALSE → 1 sec delay
- **Concept:** Pull-down with XOR toggle on single LED.

---

### 06 · Digital Input + XOR — Dual LED (`06_digitalinput_xor_dualled`)
- **Input:** P2.0 (pull-down)
- **Output:** P1.0 + P1.6 (both toggle simultaneously)
- **Logic:**
  - Condition TRUE → 0.1 sec delay
  - Condition FALSE → 1 sec delay
- **Concept:** XOR toggles both LEDs together at same time.

---

### 07 · Digital Input + Alternating LED (`07_digitalinput_alternating_led`)
- **Input:** P2.0 (pull-down)
- **Output:** P1.0 starts HIGH, P1.6 starts LOW → alternating toggle
- **Logic:**
  - Condition TRUE → 0.1 sec delay
  - Condition FALSE → 1 sec delay
- **Concept:** Because initial states are opposite, XOR toggle makes
  LEDs always alternate — one ON, one OFF at any time.

---

### 08 · Two Digital Inputs + if-else (`08_two_input_ifelse`)
- **Input:** P1.3 (pull-up) + P1.4 (pull-down)
- **Output:** P1.0 starts HIGH, P1.6 starts LOW
- **Logic (if-else):**

  | Condition | Delay |
  |---|---|
  | P1.3 HIGH only | 3 sec |
  | P1.3 HIGH + P1.4 HIGH | 0.5 sec |
  | Both LOW | 1 sec |
  | P1.4 HIGH only | 0.1 sec |

- **Concept:** Multi-input decision making with two different resistor types.

---

### 09 · Two Digital Inputs + switch-case (`09_two_input_switchcase`)
- **Input:** P1.3 (pull-up) + P1.4 (pull-down)
- **Output:** P1.0 + P1.6 (alternating)
- **Concept:** Same logic as Program 08 rewritten using `switch-case`
  for cleaner multi-condition handling.

---

## Key Concepts Covered

| Concept | Register Used |
|---|---|
| Input direction | `P1DIR &= ~BITx` |
| Enable resistor | `P1REN \|= BITx` |
| Pull-up select | `P1OUT \|= BITx` |
| Pull-down select | `P1OUT &= ~BITx` |
| Read input | `P1IN & BITx` |
| XOR toggle | `P1OUT ^= (BIT0 + BIT6)` |

---

## Pin Summary

| Pin | Role | Resistor |
|---|---|---|
| P1.3 | Input (programs 4, 8, 9) | Pull-up |
| P1.4 | Input (programs 8, 9) | Pull-down |
| P2.0 | Input (programs 1–7) | Pull-down / Pull-up |
| P1.0 | Output LED | — |
| P1.6 | Output LED | — |

---

*02-03-2026 · MSP430 · HungKuang University · Taiwan Exchange Program*

# Lab Session — 23-03-2026
## Polling vs Interrupts · Edge Selection · Debouncing · Hold Detection

> MSP430G2553 · Code Composer Studio · HungKuang University

---

## Overview

This session introduces **hardware interrupts** on the MSP430 and contrasts
them with polling-based input reading. Concepts progress from basic polling
to debouncing, counter implementation, and hold-duration detection.

---

## Programs

### 01 · Polling — No Operation (`01_polling_no_operation.c`)
- **Input:** P1.4 (pull-down)
- **Output:** None
- **Concept:** Introduction to polling. P1.4 configured as digital input
  with pull-down resistor. No output operation performed — foundation
  for understanding input reading before interrupts.

---

### 02 · Polling — Function (`02_polling_function.c`)
- **Input:** P1.4 (pull-down)
- **Output:** P1.0 + P1.6 (alternating LED toggle)
- **Concept:** Button checked repeatedly via separate `check()` function
  called inside a loop. Demonstrates polling responsiveness limitation —
  button press can be missed during delay cycles.

---

### 03 · Interrupt — Basic (`03_interrupt_basic.c`)
- **Input:** P1.4 (pull-down)
- **Output:** P1.0 + P1.6 (alternating toggle)
- **Concept:** First hardware interrupt implementation using
  `P1IE`, `GIE`, and `#pragma vector = PORT1_VECTOR`.
  Intentionally missing `P1IFG` clear — demonstrates startup
  flag bug where CPU gets stuck in ISR forever.

---

### 04 · Interrupt — Flag Clear (`04_interrupt_flag_clear.c`)
- **Input:** P1.4 (pull-down)
- **Output:** P1.0 + P1.6 (alternating toggle)
- **Concept:** Fixes Program 03 by clearing `P1IFG` before
  enabling GIE (removes startup noise flag) and inside ISR
  (prevents re-triggering after handler completes).

---

### 05 · Interrupt — Edge Select (`05_interrupt_edge_select.c`)
- **Input:** P1.4 (pull-down)
- **Output:** P1.0 + P1.6 (alternating toggle)
- **Concept:** Introduces `P1IES` register for interrupt edge selection.
  - `P1IES &= ~BIT4` → Low to High (trigger on press)
  - `P1IES |= BIT4`  → High to Low (trigger on release)

---

### 06 · Interrupt — Counter + Bounce (`06_interrupt_counter_bounce.c`)
- **Input:** P1.4 (pull-down, rising edge)
- **Output:** `count` variable (view in CCS debugger)
- **Concept:** Counts button presses using global `count` variable.
  Demonstrates switch bounce problem — one physical press
  increments count multiple times due to signal noise.

---

### 07 · Interrupt — Debounce (`07_interrupt_debounce.c`)
- **Input:** P1.4 (pull-down, rising edge)
- **Output:** `count` variable
- **Concept:** Fixes bounce problem using software debounce.
  `if` check confirms pin still HIGH + 0.5 sec delay
  allows bounce to settle before clearing flag.
  Count now reliably increments by exactly 1 per press.

---

### 08 · Interrupt — LED Feedback (`08_interrupt_led_feedback.c`)
- **Input:** P1.4 (pull-down, rising edge)
- **Output:** P1.0 LED + `count` variable
- **Concept:** Adds visual confirmation of button press.
  LED turns ON for 0.5 sec on each confirmed press
  then turns OFF — ready for next press.
  Count visible in CCS debugger expressions window.

---

### 09 · Interrupt — Hold Reset (`09_interrupt_hold_reset.c`)
- **Input:** P1.4 (pull-down, rising edge)
- **Output:** P1.0 LED (blinks while held) + `count` variable
- **Concept:** Detects hold duration inside ISR using local
  counter `ct`. Short press increments count. Holding button
  for 3 seconds (30 × 0.1 sec) resets count to zero and exits.
  Practical pattern — short press = action, long press = reset.

---

## Key Concepts Covered

| Concept | Register / Keyword |
|---|---|
| Interrupt enable (pin) | `P1IE \|= BIT4` |
| Interrupt enable (global) | `__bis_SR_register(GIE)` |
| Edge selection | `P1IES` |
| Flag register | `P1IFG` |
| ISR declaration | `#pragma vector = PORT1_VECTOR` |
| Switch bounce | Signal noise on press/release |
| Software debounce | `if` check + `__delay_cycles()` |
| Hold detection | `while(P1IN & BIT4)` + local counter |

---

## Polling vs Interrupt Comparison

| | Polling | Hardware Interrupt |
|---|---|---|
| Button check | Manually at fixed points | Instantly at any moment |
| Can miss press? | Yes | No |
| CPU usage | Always checking | Waits, reacts when needed |
| Analogy | Checking phone every few minutes | Phone rings and interrupts you |

---

### 10 · Interrupt — Hold Reset + Dual LED (`10_interrupt_hold_reset_dual_led.c`)
- **Input:** P1.4 (pull-down, rising edge)
- **Output:** P1.0 LED (blinks while held) + P1.6 LED (reset indicator)
- **Concept:** Builds on Program 09 by adding P1.6 as a dedicated
  reset indicator. Short press increments count — P1.0 blinks
  while button is held. Holding for 3 seconds (30 × 0.1 sec)
  resets count to zero and P1.6 flashes for 0.5 sec to visually
  confirm the reset. Both LEDs start OFF.

  | Action | P1.0 | P1.6 |
  |---|---|---|
  | Button held | Blinks every 0.1 sec | OFF |
  | Hold 3 sec → reset | OFF | Flashes 0.5 sec |
  | Idle | OFF | OFF |

## Pin Summary

| Pin | Role |
|---|---|
| P1.4 | Input — push button (pull-down) |
| P1.0 | Output — press indicator LED (programs 1-5, 8-10) |
| P1.6 | Output — LED indicator (programs 1-5) + reset indicator (program 10) |

## Debugging Tip

For programs 06-09, open the **Expressions window** in CCS
and add `count` to watch the value increment in real time
while the program runs.

---

*23-03-2026 · MSP430 · HungKuang University · Taiwan Exchange Program*

# Lab Session — 30/03/2026

> MSP430 Microprocessor Application · HungKuang University · Taiwan

---

## Overview

This session covered **5 programs** split into two categories:
- Reset function implementation (Programs 1 & 2)
- Interrupt-driven parking lot system (Programs 3, 4 & 5)

---

## Program 1 — Reset Button (Basic)

**Concept:** Hold button to trigger reset, release early to blink LED2.

| Condition | What Happens |
|---|---|
| Button held 3+ sec | `ct >= 3` → `resetToDefault()` → LED1 blinks 5 times → back to `while(1)` |
| Button released early | first while exits → `while(1)` → LED2 blinks every 0.1s |
| Button never pressed | first while never enters → `while(1)` → LED2 blinks |

**Key concept — XOR toggle blink count:**
```
6 toggles  = 3 blinks   (ON OFF ON OFF ON OFF)
10 toggles = 5 blinks
Formula: blinks = toggles / 2
Always use EVEN number of toggles!
```

**Pins:** `P1.4` input (pull-down) · `P1.0` LED1 · `P1.6` LED2

---

## Program 2 — Reset Button (Forward Declaration)

**Concept:** Same as Program 1 with proper C structure fixes.

**Fixes applied:**
- `void resetToDefault(void);` forward declaration added before `main()`
- `char ct = 0;` initialized properly
- `10 toggles` used for correct 5 blinks

**Key concept — forward declaration:**
```
C compiler reads top to bottom.
If a function is called before it is defined,
compiler throws an error.
A forward declaration tells the compiler the
function exists before it sees the definition.
```

---

## Program 3 — Parking Lot (Basic Interrupt)

**Concept:** Interrupt-driven car entry/exit counter with LED indicators.
No forceful reset. LED status only.

**Pins:**

| Pin | Role | Config |
|---|---|---|
| P1.3 | Entry sensor | Pull-up, HIGH to LOW |
| P1.4 | Exit sensor | Pull-down, LOW to HIGH |
| P1.0 | RED LED | Parking FULL |
| P1.6 | GREEN LED | Parking AVAILABLE |

**LED indicator:**
```
space > 0  → GREEN (BIT6) ON  → available
space == 0 → RED   (BIT0) ON  → full
```

**ISR logic:**
```
Car enters → P1.3 triggers → space-- (if space > 0)
Car exits  → P1.4 triggers → space++ (if space < SPACE)
```

**Key registers:**
```
P1IE  |= (BIT3+BIT4)  → interrupts enabled
P1IES |=  BIT3         → HIGH to LOW edge (entry)
P1IES &= ~BIT4         → LOW to HIGH edge (exit)
P1IFG &= ~(BIT3+BIT4) → clear flags
__bis_SR_register(GIE) → global interrupt ON
```

> Note: `.c` reference file only — no forceful reset in this version.

---

## Program 4 — Parking Lot V3 (Forceful Reset in Main Loop)

**Concept:** Extends Program 3 — forceful `space = 0` placed inside `while()` main loop.

**Additional block in while loop:**
```c
if((P1IN & (BIT3 + BIT4)) == BIT4){
    __delay_cycles(500000);
    P1IFG &= ~(BIT3 + BIT4);
    space = 0;   // emergency reset
}
```

**Behavior:**
```
BIT4 alone HIGH → space forced to 0 → emergency reset
Debounce 500ms added per ISR case
```

**Changes from Program 3:**
- `#define SPACE 6` added for clean constant
- `space > 0` and `space < SPACE` bounds checks added
- Forceful reset handled in `while()` loop
- 500ms debounce delay inside each ISR case

---

## Program 5 — Parking Lot V4 (Forceful Reset in ISR)

**Concept:** Forceful reset moved from `while()` into ISR.
Single 500ms delay placed at ISR entry covers all cases.

**ISR structure:**
```
Enter ISR
  |
  delay 500ms          ← ONE delay covers all cases
  |
  if(BOTH BIT3+BIT4)   ← both pressed = emergency reset
      space = 0
      flags cleared
  |
  if(BIT3 only)        ← car entry
      space--
      BIT3 flag cleared
  |
  else if(BIT4 only)   ← car exit
      space++
      BIT4 flag cleared
```

**Changes from V3:**
- Emergency reset moved from `while()` → ISR
- Single 500ms delay at ISR entry replaces per-case delays
- Reset block in `while()` commented out

---

## Version Comparison — V3 vs V4

```
+------------------+--------------------+--------------------+
| Feature          | V3 (Program 4)     | V4 (Program 5)     |
+------------------+--------------------+--------------------+
| Reset location   | while() main loop  | ISR                |
| Delay position   | Inside each if     | Once at ISR entry  |
| Code lines       | Repeated delays    | Single delay       |
| Debounce         | Per condition      | All conditions     |
| Readability      | Repetitive         | Clean              |
| Best practice    | No                 | Yes                |
+------------------+--------------------+--------------------+
```

---

## Key Concepts Covered

`P1IE` `P1IES` `P1IFG` `__bis_SR_register(GIE)`
`ISR — interrupt subroutine` `Forward declaration` `#define constant`
`XOR toggle` `Debounce delay` `Pull-up / Pull-down`
`HIGH to LOW edge` `LOW to HIGH edge` `Emergency reset`

---

> **Note:** Each folder contains only `.c` reference source files.
> Full CCS project folders are not committed.

*MSP430 · Texas Instruments · HungKuang University · Taiwan Exchange Program*

# Lab Session — 30/03/2026

> MSP430 Microprocessor Application · HungKuang University · Taiwan

---

## Overview

This session covered **5 programs** split into two categories:
- Reset function implementation (Program 1)
- Interrupt-driven parking lot system (Programs 2, 3, 4 & 5)

---

## Program 1 — Reset Button (with Forward Declaration)

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

**Key concept — forward declaration:**
```
C compiler reads top to bottom.
If function is called before it is defined,
compiler throws an error.
Forward declaration tells compiler the
function exists before it sees the definition.

void resetToDefault(void);  <- add before main()
```

**Pins:** `P1.4` input (pull-down) · `P1.0` LED1 · `P1.6` LED2

---

## Program 2 — Parking Lot (Basic Interrupt)

**Concept:** Basic interrupt-driven car counter using `__no_operation()`
as a debugger breakpoint to inspect `space` value. No LED, no bounds check.

**Pins:**

| Pin | Role | Config |
|---|---|---|
| P1.3 | Entry sensor | Pull-up, HIGH to LOW |
| P1.4 | Exit sensor | Pull-down, LOW to HIGH |

**ISR logic:**
```
Car enters → P1.3 triggers → __no_operation() → space--
Car exits  → P1.4 triggers → __no_operation() → space++
```

**Key registers:**
```
P1IE  |= (BIT3+BIT4)  → interrupts enabled
P1IES |=  BIT3         → HIGH to LOW edge (entry)
P1IES &= ~BIT4         → LOW to HIGH edge (exit)
P1IFG &= ~(BIT3+BIT4) → clear flags
__bis_SR_register(GIE) → global interrupt ON
```

**Important notes:**
```
- space = 6 (set after GIE)
- No LED indicator in this version
- No bounds check (no space > 0 or space < 6)
- __no_operation() used as breakpoint
  to inspect space value in CCS debugger
```

---

## Program 3 — Parking Lot (LED Indicator, No Forceful Reset)

**Concept:** Extends Program 2 — adds LED indicator for space status.
No forceful reset in this version.

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

**Changes from Program 2:**
- `#define SPACE 6` added for clean constant
- `P1.0` and `P1.6` LEDs added as output
- `space > 0` and `space < SPACE` bounds checks added
- No forceful reset

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
- Forceful reset handled in `while()` loop
- 500ms debounce delay inside each ISR case

---

## Program 5 — Parking Lot V4 (Forceful Reset in ISR)

**Concept:** Forceful reset moved from `while()` into ISR.
Single 500ms delay at ISR entry covers all cases.

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

## Parking System Evolution

```
+------------------+--------+-------+---------+---------+
| Feature          | Prog 2 | Prog 3| Prog 4  | Prog 5  |
+------------------+--------+-------+---------+---------+
| LED indicator    | No     | Yes   | Yes     | Yes     |
| Bounds check     | No     | Yes   | Yes     | Yes     |
| Forceful reset   | No     | No    | while() | ISR     |
| Debounce delay   | No     | No    | Per case| Entry   |
| #define SPACE    | No     | Yes   | Yes     | Yes     |
| Best practice    | Basic  | Good  | Better  | Best    |
+------------------+--------+-------+---------+---------+
```

---

## Key Concepts Covered

`P1IE` `P1IES` `P1IFG` `__bis_SR_register(GIE)`
`ISR — interrupt subroutine` `Forward declaration` `#define constant`
`XOR toggle` `Debounce delay` `Pull-up / Pull-down`
`HIGH to LOW edge` `LOW to HIGH edge` `Emergency reset`
`__no_operation()` `CCS debugger breakpoint`

---

> **Note:** Each folder contains only `.c` reference source files.
> Full CCS project folders are not committed.

*MSP430 · Texas Instruments · HungKuang University · Taiwan Exchange Program*

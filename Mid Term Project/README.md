# 🏋️ MSP430 Gym Equipment Timer

A microcontroller-based gym timer built on the **MSP430**, designed to help users and gym staff manage equipment usage time fairly. The system tracks active sessions, enforces time limits per workout mode, alerts users when they're approaching or exceeding their limit, and enforces a rest/cleaning period between users.

---

## Features

- **3 workout modes** — Cardio (15 min), Upper Body Training (5 min), Leg Training (5 min)
- **Visual LED indicators** — color-coded status at a glance (green / yellow / red / blue)
- **Buzzer alerts** — short beeps for warnings, long beeps for overtime, emergency pattern
- **BCD 7-segment display** — shows current mode or minutes remaining
- **Rest period enforcement** — 1-minute cooldown between users with a long beep signal
- **Emergency stop** — instantly halts the session and triggers all LEDs + rapid beep
- **Debounced button inputs** — stable readings for all 4 buttons

---

## Hardware

| Component | Pin |
|---|---|
| Red LED | P1.0 |
| Buzzer | P1.2 |
| Emergency button | P1.1 (pull-up) |
| Entry button | P1.3 (pull-up) |
| Exit button | P1.4 (pull-down) |
| Yellow LED | P1.5 |
| Green LED | P1.6 |
| Blue LED | P1.7 |
| Mode button | P2.0 (pull-up) |
| BCD display (4-bit) | P2.2 – P2.5 |

---

## How It Works

### Modes

| Mode | Button Presses | Max Time | Warning At |
|---|---|---|---|
| 0 — Cardio | 0 (default) | 15 min | 12 min |
| 1 — Upper Body | 1 press | 5 min | 4 min |
| 2 — Leg Training | 2 presses | 5 min | 4 min |

Cycle through modes with the **mode button** (P2.0). Mode changes are only allowed when no session is active.

### Session Flow

```
[Red LED on] → Press ENTRY → Session starts
      ↓
  Safe zone (silent, LEDs off)
      ↓
  Warning zone → Yellow LED on, Green blinks, short beep
      ↓
  Overtime → Red LED on, long beep every second
      ↓
  Press EXIT → Rest period begins (Blue LED, long beep x30s)
      ↓
  After 60s → Red LED signals next user
```

### LED Status Reference

| LED Color | Meaning |
|---|---|
| 🔴 Red | Session started / overtime / next user ready |
| 🟡 Yellow | Approaching time limit (warning zone) |
| 🟢 Green (blinking) | Warning zone active (blinks alongside yellow) |
| 🔵 Blue | Rest / cleaning period in progress |
| All 4 on | Emergency stop triggered |

### Display

The BCD 4-bit display (P2.2–P2.5) shows:
- **Mode number** (0, 1, or 2) when idle or at session start
- **Minutes remaining** (0–9) while a session is active

---

## Building & Flashing

This project targets the **MSP430** family and is developed using **Code Composer Studio (CCS)** or **msp430-gcc**.

### With Code Composer Studio

1. Clone this repo
2. Open CCS → `File > Import > CCS Project`
3. Select the project folder
4. Build (`Ctrl+B`) and flash to your target board

### With msp430-gcc (command line)

```bash
msp430-gcc -mmcu=msp430g2553 -o timer.elf main.c
mspdebug rf2500 "prog timer.elf"
```

> Adjust `-mmcu` to match your specific MSP430 variant.

---

## Project Structure

```
.
└── main.c        # All source code (single-file project)
```

---

## Possible Improvements

- Add UART output for session logging
- Use Timer_A for accurate 1-second intervals instead of `__delay_cycles`
- Expand to a 2-digit display for longer session support
- Add user count tracking per session

---

## License

This project is released under the [MIT License](LICENSE). Feel free to use, modify, and build upon it.

---

## Author

Created as a midterm embedded systems project. Contributions and feedback welcome — open an issue or submit a pull request!

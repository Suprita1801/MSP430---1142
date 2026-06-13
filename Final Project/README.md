# 🏋️ SmartGym Monitor

An embedded systems project built on the **MSP430G2553** microcontroller that monitors gym sessions in real time — tracking workout time, temperature, motion, and providing emergency alerts — with Bluetooth data transmission to a trainer's phone via the **HM-10 BLE module**.

---

## 📋 Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Hardware Components](#hardware-components)
- [Pin Map](#pin-map)
- [Wiring Guide](#wiring-guide)
- [Session Modes](#session-modes)
- [LCD Screens](#lcd-screens)
- [Buzzer Patterns](#buzzer-patterns)
- [Bluetooth — LightBlue App](#bluetooth--lightblue-app)
- [Project Structure](#project-structure)
- [How to Build & Flash](#how-to-build--flash)
- [Development Stages](#development-stages)

---

## Overview

SmartGym Monitor is a standalone gym session management system designed for single-user gym environments. The user interacts via push buttons to enter the gym, select a workout mode, and confirm their session. The system then:

- Counts session time and displays it live on a 16×2 LCD
- Warns the user when time is almost up
- Monitors gym temperature and alerts if it gets too hot or cold
- Detects user motion via a PIR sensor — alerts if no movement for 30 seconds
- Provides an emergency button for immediate assistance
- Transmits all events to a trainer's phone over Bluetooth (BLE)

---

## Features

| Feature | Details |
|---|---|
| **5 Push Buttons** | Entry, Exit, Mode, Confirm, Emergency |
| **16×2 I2C LCD** | Displays all states, timer, temperature |
| **Session Timer** | Live MM:SS countdown per workout mode |
| **3 Workout Modes** | Cardio, Upper Body, Leg Training |
| **Side Switch Alert** | Upper and Lower modes split into 5+5 min with buzzer alert to switch sides |
| **Warning Alert** | Buzzer + LCD warn when time is almost up |
| **Temperature Sensor** | TMP36 reads gym temp every 10 seconds |
| **PIR Motion Sensor** | HC-SR501 detects user presence — alerts after 30s of no motion |
| **Emergency Button** | Triggers alarm + auto-resets after 10 seconds |
| **Buzzer** | 9 unique sound patterns for each event |
| **Bluetooth BLE** | HM-10 transmits all events to LightBlue app |
| **Debug UART** | CP2102 USB-to-TTL for serial monitor verification |

---

## Hardware Components

| Component | Model | Purpose |
|---|---|---|
| Microcontroller | MSP430G2553 (Launchpad) | Main controller |
| LCD Display | 16×2 with PCF8574 I2C backpack | User display |
| Temperature Sensor | TMP36 | Gym temperature |
| Motion Sensor | HC-SR501 PIR | User presence detection |
| Bluetooth Module | HM-10 BLE | Trainer notifications |
| USB-to-TTL | CP2102 (MTARDC02102) | Debug serial monitor |
| Buzzer | 3-pin active-LOW module | Audio alerts |
| Buttons | Push buttons × 5 | User input |
| Resistors | 10kΩ + 20kΩ | Voltage divider for PIR |
| Power | Arduino UNO | 5V supply for HC-SR501 |

---

## Pin Map

```
PIN     COMPONENT              NOTES
────────────────────────────────────────────────────
P1.0    TMP36 VOUT             ADC channel A0
P1.1    CP2102 TXD             MSP430 RX  (debug UART)
P1.2    CP2102 RXD             MSP430 TX  (debug UART)
P1.3    Entry Button           → GND  (internal pull-up)
P1.4    Exit  Button           → 3.3V (internal pull-down)
P1.5    Buzzer I/O             3-pin active-LOW module
P1.6    HC-SR501 ECHI          via 10kΩ/20kΩ voltage divider
P2.0    Mode Button            → GND  (internal pull-up)
P2.1    Confirm Button         → GND  (internal pull-up)
P2.2    Emergency Button       → GND  (internal pull-up)
P2.3    LCD SDA                Software I2C
P2.4    LCD SCL                Software I2C
P2.5    HM-10 RXD              Software UART TX → BLE
```

---

## Wiring Guide

### Buttons
| Button | Pin | Other End |
|---|---|---|
| Entry | P1.3 | GND (pull-up) |
| Exit | P1.4 | 3.3V (pull-down) |
| Mode | P2.0 | GND (pull-up) |
| Confirm | P2.1 | GND (pull-up) |
| Emergency | P2.2 | GND (pull-up) |

### TMP36 Temperature Sensor
```
TMP36 Pin 1 (left,  flat face) → 3.3V
TMP36 Pin 2 (middle)           → P1.0
TMP36 Pin 3 (right)            → GND
```

### HC-SR501 PIR — Voltage Divider
The HC-SR501 outputs 5V logic but MSP430 is 3.3V — a voltage divider steps it down safely.

```
HC-SR501 VCC   → Arduino 5V
HC-SR501 GND   → Common GND
HC-SR501 ECHI  → 10kΩ (R1) top leg

Breadboard layout:
  Row 10: ECHI wire  ──── 10kΩ top leg
  Row 14: 10kΩ bottom leg + 20kΩ top leg + P1.6 wire  ← junction
  Row 18: 20kΩ bottom leg ──── GND rail

Result: 5V × 20k/(10k+20k) = 3.33V → safe for MSP430 ✓
```

> ⚠️ **Important:** Arduino GND, MSP430 GND, and resistor GND must all share the same common GND rail on the breadboard.

### LCD (PCF8574 I2C Backpack)
```
LCD VCC → 3.3V (or 5V depending on your module)
LCD GND → GND
LCD SDA → P2.3
LCD SCL → P2.4
```
> Default I2C address: `0x27`. If LCD stays blank, change to `0x3F` in `lcd_i2c.h`.

### Buzzer (3-pin active-LOW module)
```
Buzzer VCC → 3.3V
Buzzer I/O → P1.5
Buzzer GND → GND
```

### HM-10 Bluetooth BLE
```
HM-10 VCC → 3.3V  (NOT 5V)
HM-10 GND → GND
HM-10 RXD → P2.5
HM-10 TXD → unconnected (for now)
```

### CP2102 Debug UART
```
CP2102 TXD → P1.1
CP2102 RXD → P1.2
CP2102 GND → GND
```

---

## Session Modes

| Mode | Duration | Structure | Warning | Rest |
|---|---|---|---|---|
| **CARDIO** | 20 min | Continuous | 60s left | 5 min |
| **UPPER BODY** | 10 min | 5 min left arm + 5 min right arm | 4 min left | 3 min |
| **LEG TRAIN** | 10 min | 5 min left leg + 5 min right leg | 4 min left | 3 min |

**Side switch:** At the 5-minute mark for Upper and Lower modes, the buzzer fires and LCD shows `SWITCH SIDE NOW!` or `SWITCH LEG NOW!` to remind the user to swap sides.

---

## LCD Screens

```
Power on:        SMARTGYM MON
                 TEMP:24 C  GOOD

Idle:            SMARTGYM MON
                 TEMP:28 C ON AC

Mode select:     SELECT MODE:
                 > CARDIO

Confirmed:       MODE CONFIRMED!
                 UPPER 5+5=10MIN

Active session:  UPPER  03:22
                 LEFT   06:38

Warning:         UPPER  09:00
                 !WARN  01:00

PIR no motion:   UPPER  05:10
                 NO MOV 12s LEFT

Side switch:     UPPER  05:00
                 SWITCH SIDE NOW!

Rest:              REST PERIOD
                 REST   03:00

Emergency:       !! EMERGENCY !!
                  HELP IS COMING

Auto reset:      EMERGENCY RESET
                   PRESS ENTRY
```

---

## Buzzer Patterns

| Event | Pattern | Sound |
|---|---|---|
| Entry | 2 short beeps | pip pip |
| Exit | 1 long beep | beeeeep |
| Mode cycle | 1 quick click | tick |
| Confirm | 3 rising beeps | pip pip BEEP |
| Warning / Side switch | 3 slow beeps | beep beep beep |
| Temperature alert | 2 fast beeps | pip pip |
| Rest start | 2 medium beeps | beep beep |
| Rest end | 3 medium beeps | beep beep beep |
| PIR no motion | 4 long pulses | BEEP BEEP BEEP BEEP |
| Emergency | 8 rapid bursts | BZBZBZBZBZBZBZBZ |

---

## Bluetooth — LightBlue App

### Setup
1. Install **LightBlue** app (iOS or Android)
2. Power on the system
3. Open LightBlue → **Scan**
4. Connect to **HMSoft**
5. Tap characteristic **UUID FFE1**
6. Tap **Listen for Notifications**

### Messages received by trainer

```
=== SmartGym Monitor FINAL ===
All systems ready
ENTRY|IN_GYM
MODE:CARDIO
SESSION:ACTIVE
TEMP:24C|GOOD
TEMP:29C|ON AC
PIR:NO MOTION 30S ALERT
UPPER:SWITCH SIDE
WARN:TIME ALMOST UP
REST:START
REST:DONE|PRESS ENTRY
EMERGENCY!!
EMERGENCY:RESET|PRESS ENTRY
EXIT|IDLE
```

---

## Project Structure

```
SmartGym-Monitor/
│
├── smartgym_complete.c     ← Main source file (final integration)
├── lcd_i2c.h               ← LCD I2C header
├── lcd_i2c.c               ← LCD I2C driver (software I2C)
├── README.md               ← This file
│
└── stages/                 ← Step-by-step development files
    ├── stage1_buttons.c        Stage 1: Entry + Exit buttons
    ├── stage2_buttons.c        Stage 2: All 5 buttons
    ├── stage3_lcd.c            Stage 3: I2C LCD added
    ├── stage4_buzzer.c         Stage 4: Buzzer patterns
    ├── stage5_timer.c          Stage 5: Session timer
    ├── stage6_temperature.c    Stage 6: TMP36 + PIR + BLE
    ├── pir_test.c              PIR standalone test
    └── hm10_ble_test.c         HM-10 BLE standalone test
```

---

## How to Build & Flash

### Requirements
- **Code Composer Studio (CCS)** v12 or later
- **MSP430 GCC toolchain** (installed via CCS)
- **MSP430Ware** (for calibration constants)

### Steps

1. Open CCS → **File → New → CCS Project**
2. Target: `MSP430G2553`
3. Add these files to the project:
   - `smartgym_complete.c`
   - `lcd_i2c.h`
   - `lcd_i2c.c`
4. Build: `Ctrl + B`
5. Connect Launchpad via USB
6. Flash: `Run → Debug` then `Run → Resume`

### Serial Monitor (debug)
- Open **CCS Terminal** or PuTTY / Tera Term
- Port: COM port of CP2102
- Settings: **9600 baud | 8N1 | No flow control**

---

## Development Stages

This project was built **incrementally** — each stage verified before adding the next component. This approach makes debugging much easier.

| Stage | Component Added | Verified Via |
|---|---|---|
| 1 | Entry + Exit buttons (P1.3, P1.4) | Serial monitor |
| 2 | Mode, Confirm, Emergency buttons (P2.0–P2.2) | Serial monitor |
| 3 | I2C LCD 16×2 (P2.3, P2.4) | LCD display |
| 4 | Buzzer — 9 unique patterns (P1.5) | Audio |
| 5 | Session timer — TimerA software prescaler | LCD MM:SS |
| 6 | TMP36 temperature sensor (P1.0 ADC) | LCD + serial |
| 7 | HC-SR501 PIR motion sensor (P1.6) | LCD + serial |
| 8 | HM-10 BLE Bluetooth (P2.5 software UART) | LightBlue app |

---

## Notes

- **Clock:** 16 MHz calibrated DCO — required for accurate UART and timer
- **UART baud:** 9600 baud for both CP2102 (hardware) and HM-10 (software bit-bang)
- **Timer accuracy:** `SMCLK/8 = 2MHz`, `CCR0 = 49999` → 25ms ISR × 40 = 1 second exactly
- **PIR warm-up:** HC-SR501 needs ~30 seconds after power-on before reliable detection
- **Buzzer logic:** Active-LOW 3-pin module — `I/O LOW = ON`, `I/O HIGH = OFF`
- **Voltage divider:** Required to step down PIR 5V output to 3.33V for MSP430 input

---

## License

This project is open source and free to use for educational purposes.

---

*Built with ❤️ using MSP430G2553 — SmartGym Monitor Project*

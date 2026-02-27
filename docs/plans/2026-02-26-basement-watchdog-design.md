# Basement Watchdog - System Design

**Date:** 2026-02-26
**Status:** Approved (Revised)
**Actual Cost:** $65.05

## Overview

A wireless basement environment monitoring system for detecting sump pump failures. The system consists of a basement sensor unit that monitors environmental conditions and pump activity, and a display unit for the living area that shows current status and alerts.

## Problem Statement

An old sump pump with a sticky float causes periodic basement flooding (up to 1 ft of water). The basement is inconvenient to check regularly, especially in winter. A remote monitoring solution is needed that:
- Detects water presence directly
- Monitors pump activity (running/not running)
- Monitors environmental conditions (temp, humidity)
- Alerts to power failures (pump won't run without power)
- Works without WiFi (more reliable)
- Provides a display viewable from the living area

## Requirements

### Functional Requirements
- Sense: Temperature, Humidity, Water Presence, Pump Activity, AC Power
- Transmit sensor data every 5 minutes via LoRa 915MHz
- Display current readings on OLED screen
- Alert (sound) on both units when thresholds exceeded
- Battery backup on sensor unit to survive power outages
- Detect pump running via vibration sensor (interrupt-driven, never misses)

### Non-Functional Requirements
- Distance: ~50-80 ft through one floor
- Budget: < $100 (actual: $65.05)
- Power: USB with battery backup (sensor), USB (display)
- Environment: Damp basement
- User skill: Can follow tutorials, some Arduino/ESP32 experience

## System Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         BASEMENT SENSOR UNIT                            │
│                                                                          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │              Heltec WiFi LoRa 32 V3                               │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐               │  │
│  │  │  ESP32-S3   │  │ SX1262 LoRa │  │ 0.96" OLED  │               │  │
│  │  │  240MHz     │  │   915MHz    │  │   Display   │               │  │
│  │  └─────────────┘  └─────────────┘  └─────────────┘               │  │
│  │  ┌─────────────┐  ┌─────────────┐                                │  │
│  │  │  Built-in   │  │  3000mAh    │                                │  │
│  │  │  Button     │  │  Battery    │                                │  │
│  │  └─────────────┘  └─────────────┘                                │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│         │                                                                │
│    ┌────┴────────────────────────────────────────────────────┐          │
│    │  External Sensors (via RJ11/RJ45):                      │          │
│    │  • DHT22 (Temp/Humidity) - direct wired                 │          │
│    │  • Float switch (water detect)                          │          │
│    │  • SW-420 Vibration sensor (pump activity) - interrupt  │          │
│    │  • Voltage divider on 5V USB (power detect)             │          │
│    └─────────────────────────────────────────────────────────┘          │
│                                                                          │
│    [Buzzer] ◄── Local alerts (direct wired)                             │
│                                                                          │
│    Power: USB-C wall adapter → Battery (auto-switches on power loss)    │
└─────────────────────────────────────────────────────────────────────────┘
                              │
                              │ LoRa 915MHz (~50-80 ft through floor)
                              │ Transmits every 5 minutes
                              │ + Instant alert on water/pump failure
                              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         DISPLAY UNIT (Side Table)                       │
│                                                                          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │              Heltec WiFi LoRa 32 V3                               │  │
│  │  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐               │  │
│  │  │  ESP32-S3   │  │ SX1262 LoRa │  │ 0.96" OLED  │               │  │
│  │  │  240MHz     │  │   915MHz    │  │   Display   │               │  │
│  │  └─────────────┘  └─────────────┘  └─────────────┘               │  │
│  │  ┌─────────────┐  ┌─────────────┐                                │  │
│  │  │  Built-in   │  │  3000mAh    │                                │  │
│  │  │  Button     │  │  Battery    │                                │  │
│  │  └─────────────┘  └─────────────┘                                │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│    [Buzzer] ◄── Alert when thresholds exceeded (direct wired)           │
│    [Button] ◄── Acknowledge alerts (built-in on Heltec)                 │
│                                                                          │
│    Power: USB-C wall adapter (battery as backup)                        │
└─────────────────────────────────────────────────────────────────────────┘
```

## Data Flow

### Normal Operation
1. Basement unit reads sensors every 5 minutes
2. Transmits sensor packet via LoRa
3. Display unit receives packet, updates OLED
4. Both units sleep/idle between readings

### Interrupt-Driven Events
1. **Pump runs:** Vibration sensor triggers interrupt → logs pump activity
2. **Water detected:** Float switch triggers interrupt → immediate LoRa alert
3. **Power loss:** Voltage divider detects 5V drop → immediate LoRa alert

### Alert Flow
1. Critical condition detected
2. Local buzzer sounds on basement unit
3. LoRa alert sent immediately (doesn't wait for 5-min cycle)
4. Display unit receives alert, sounds buzzer
5. User presses button to acknowledge → silences buzzer
6. Visual alert remains until condition clears

## Hardware Details

### Heltec WiFi LoRa 32 V3 Specifications

| Feature | Specification |
|---------|---------------|
| MCU | ESP32-S3FN8 (240 MHz dual-core) |
| LoRa | SX1262, 915MHz (US), 21dBm TX |
| Display | 0.96" OLED, 128x64 pixels |
| Battery | JST 1.25 connector, built-in charging |
| USB | Type-C |
| GPIO | 7x ADC, 3x UART, 2x I2C, 2x SPI |

### Basement Sensor Unit - Pin Connections

| Heltec Pin | Connected To | Notes |
|------------|--------------|-------|
| GPIO 2 | DHT22 data | With 10K pullup |
| GPIO 4 | Float switch | Internal pullup, interrupt |
| GPIO 5 | SW-420 vibration | Interrupt-driven |
| GPIO 6 | Buzzer | Active buzzer |
| GPIO 7 | 5V detect | Voltage divider (10K+10K) |
| Built-in | Button | Alert acknowledge |
| Built-in | OLED | Local status display |
| Built-in | LoRa | SX1262 915MHz |
| JST 1.25 | 3000mAh battery | Auto-switchover |

### Display Unit - Pin Connections

| Heltec Pin | Connected To | Notes |
|------------|--------------|-------|
| GPIO 6 | Buzzer | Active buzzer |
| Built-in | Button | Alert acknowledge |
| Built-in | OLED | Status display |
| Built-in | LoRa | SX1262 915MHz |
| JST 1.25 | 3000mAh battery | Backup (optional) |

### AC Power Detection Circuit

```
USB 5V ──┬── 10K resistor ──┬── 10K resistor ── GND
                            │
                       GPIO 7 (ADC)
                       Reads ~2.5V when power on
                       Reads ~0V when power fails
```

### Sensor Connectors

Using RJ11 or RJ45 connectors for external sensors:

**RJ11 (4-pin) for simple sensors:**
| Pin | Function |
|-----|----------|
| 1 | VCC (3.3V) |
| 2 | Signal |
| 3 | GND |
| 4 | (spare) |

**RJ45 (8-pin) for sensors needing more connections:**
| Pin | Function |
|-----|----------|
| 1-2 | VCC (3.3V) |
| 3-4 | Signal(s) |
| 5-6 | GND |
| 7-8 | (spare) |

## Alert Thresholds

| Sensor | Warning | Critical |
|--------|---------|----------|
| Water | Any detection | N/A (always critical) |
| AC Power | N/A | Power loss |
| Temperature | < 40°F or > 90°F | < 32°F or > 100°F |
| Humidity | > 80% | > 95% |
| Pump | Running > 5 min | Not run in 7 days (wet season) |
| No signal | > 15 min | > 30 min |

### Alert Behavior

- **Warning:** OLED shows warning icon, no sound
- **Critical:** Buzzer beeps every 30 sec until acknowledged
- **Water detected or Power loss:** Immediate continuous alarm for 10 seconds, then periodic beeps
- **Acknowledge:** Press built-in button to silence buzzer (visual alert remains)

## OLED Display Layout

Normal state (128x64 pixels):
```
┌────────────────────────┐
│ BASEMENT     12:34 PM  │
│ ──────────────────────│
│  Temp: 62°F   Hum: 45% │
│  Water: DRY   Pwr: OK  │
│  Pump: Idle            │
│ ──────────────────────│
│  Status: NORMAL   87%  │
└────────────────────────┘
```

Alert state:
```
┌────────────────────────┐
│ !! ALERT !!   12:34 PM │
│ ══════════════════════│
│                        │
│   WATER DETECTED!      │
│                        │
│ ══════════════════════│
│  Press button to ack   │
└────────────────────────┘
```

## Bill of Materials (Actual Order)

| Item | Qty | Price |
|------|-----|-------|
| Heltec ESP32 LoRa V3 + 3000mAh batteries | 2+2 | $52.99 |
| DHT22 Temperature/Humidity Sensor | 4-pack | $9.99 |
| Float Switch (PP plastic) | 5-pack | $9.99 |
| SW-420 Vibration Sensor | 5-pack | $5.88 |
| Active Piezo Buzzer | 5-pack | $6.99 |
| **Subtotal** | | $85.84 |
| Tax | | $5.15 |
| Gift Card | | -$25.94 |
| **Total Paid** | | **$65.05** |

### User-Provided Items
- 3D printed enclosures
- Resistor kit (for voltage divider)
- RJ11/RJ45 connectors and cables
- USB-C wall adapters (2)

## Software Components

### Sensor Unit Firmware
- ESP32 deep sleep with timer wake (5 min)
- Interrupt handlers for water + vibration sensors
- DHT22 reading routine
- LoRa packet transmission (RadioLib)
- Local alert logic with buzzer
- Battery voltage monitoring
- OLED status display (U8g2 library)
- Power failure detection

### Display Unit Firmware
- LoRa receive (always listening)
- OLED display driver (U8g2 library)
- Alert state machine
- Button interrupt for acknowledge
- Buzzer control
- Time tracking (millis-based, no RTC)

### Libraries Required
- **RadioLib** - LoRa communication (supports SX1262)
- **DHT sensor library** - Temperature/humidity
- **U8g2** - OLED display
- **Heltec ESP32 library** - Board support

### LoRa Packet Format

```
Byte 0:     Packet type (0x01=status, 0x02=alert)
Byte 1-2:   Temperature (int16, tenths of degree F)
Byte 3:     Humidity (uint8, percent)
Byte 4:     Status flags:
            - Bit 0: Water detected
            - Bit 1: Power OK
            - Bit 2: Pump running
            - Bit 3-7: Reserved
Byte 5:     Battery percent
Byte 6:     Pump run count (last hour)
Byte 7:     Checksum
```

## Testing Strategy

1. **Unit Testing:** Test each sensor individually
2. **LoRa Range Test:** Verify communication through floor
3. **Power Failure Test:** Verify battery backup and 5V detection
4. **Water Detection Test:** Test float switch response
5. **Vibration Test:** Test pump detection with running motor
6. **Alert Test:** Verify buzzer on both units
7. **Acknowledge Test:** Verify button silences alarm
8. **Endurance Test:** Run for 24+ hours monitoring

## Enclosure Design

### Basement Unit
- Use existing Heltec V3 3D printed case design
- Add cutouts/holes for:
  - RJ11/RJ45 jacks (sensors)
  - Buzzer mount or sound holes
  - Ventilation for DHT22
  - USB-C access
  - Antenna (if external)

### Display Unit
- Use existing Heltec V3 3D printed case design
- Optimize for OLED visibility
- Add buzzer mount/holes
- USB-C access for power

## Future Enhancements (Out of Scope)

- WiFi notifications (hardware supports it)
- Web dashboard
- Data logging to SD card or cloud
- Multiple sensor nodes
- Historical graphs on display
- Integration with home automation

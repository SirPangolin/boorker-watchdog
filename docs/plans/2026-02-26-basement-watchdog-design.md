# Basement Watchdog - System Design

**Date:** 2026-02-26
**Status:** Approved
**Budget:** ~$56

## Overview

A wireless basement environment monitoring system for detecting sump pump failures. The system consists of a basement sensor unit that monitors environmental conditions and a display unit for the living area that shows current status and alerts.

## Problem Statement

An old sump pump with a sticky float causes periodic basement flooding (up to 1 ft of water). The basement is inconvenient to check regularly, especially in winter. A remote monitoring solution is needed that:
- Detects water presence directly
- Monitors environmental conditions (temp, humidity, light, sound)
- Alerts to power failures (pump won't run without power)
- Works without WiFi (more reliable)
- Provides a display viewable from the living area
- Stays under $60 total cost

## Requirements

### Functional Requirements
- Sense: Temperature, Humidity, Light Level, Sound Level, AC Power, Water Presence
- Transmit sensor data every 5 minutes via LoRa 915MHz
- Display current readings on e-ink screen
- Alert (light + sound) on both units when thresholds exceeded
- Battery backup on sensor unit to survive power outages

### Non-Functional Requirements
- Distance: ~50-80 ft through one floor
- Budget: < $60
- Power: Pass-through outlet with battery backup (sensor), USB (display)
- Environment: Damp basement, needs weather-resistant enclosure
- User skill: Can follow tutorials, some Arduino experience

## System Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                         BASEMENT SENSOR UNIT                            │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐                  │
│  │ Arduino Pro │◄──►│ SX1278 LoRa │    │ TP4056 +    │                  │
│  │ Mini 3.3V   │    │ 915MHz      │    │ 18650 Bat   │                  │
│  └──────┬──────┘    └─────────────┘    └──────┬──────┘                  │
│         │                                      │                         │
│    ┌────┴────────────────────────────────────┴────┐                     │
│    │  Sensors:                                     │                     │
│    │  • DHT22 (Temp/Humidity)                     │                     │
│    │  • LDR (Light level)                          │                     │
│    │  • Sound sensor (mic module)                  │                     │
│    │  • Water probes (flood detect)                │                     │
│    │  • AC optocoupler (power present)             │                     │
│    └───────────────────────────────────────────────┘                     │
│                                                                          │
│    [Buzzer] [LED] ◄── Local alerts                                      │
│                                                                          │
│    Power: Pass-through outlet → USB adapter → TP4056 → Battery backup   │
└─────────────────────────────────────────────────────────────────────────┘
                              │
                              │ LoRa 915MHz (~50-80 ft through floor)
                              │ Transmits every 5 minutes
                              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                         DISPLAY UNIT (Side Table)                       │
│  ┌─────────────┐    ┌─────────────┐    ┌─────────────┐                  │
│  │ Arduino     │◄──►│ SX1278 LoRa │    │ 2.9" E-ink  │                  │
│  │ Nano        │    │ 915MHz      │    │ Display     │                  │
│  └──────┬──────┘    └─────────────┘    └─────────────┘                  │
│         │                                                                │
│    [Buzzer] [LED] ◄── Alert when thresholds exceeded                    │
│                                                                          │
│    Power: USB wall adapter                                              │
└─────────────────────────────────────────────────────────────────────────┘
```

## Data Flow

1. Basement unit wakes from deep sleep every 5 minutes
2. Reads all sensors (~2 seconds)
3. Checks for alert conditions locally (triggers buzzer/LED if critical)
4. Transmits sensor packet via LoRa
5. Returns to deep sleep
6. Display unit receives packet, updates e-ink, triggers alerts if needed

## Hardware Details

### Basement Sensor Unit - Pin Connections

| Arduino Pro Mini Pin | Connected To |
|---------------------|--------------|
| D2 | DHT22 data (with 10K pullup) |
| D3 | Sound sensor digital out |
| D4 | Water sensor signal |
| D5 | Buzzer |
| D6 | Alert LED |
| D7 | AC detect optocoupler output |
| A0 | LDR (voltage divider with 10K) |
| A1 | Sound sensor analog (optional) |
| D10 | LoRa NSS |
| D11 | LoRa MOSI |
| D12 | LoRa MISO |
| D13 | LoRa SCK |
| D9 | LoRa RST |
| D8 | LoRa DIO0 |

### AC Power Detection Circuit

- Small 5V USB wall adapter powers TP4056
- Optocoupler (PC817) detects if AC adapter has power
- When AC present: optocoupler pulls D7 LOW
- When AC fails: D7 floats HIGH (internal pullup)
- Battery keeps Arduino running during outage

### Display Unit - Pin Connections

| Arduino Nano Pin | Connected To |
|-----------------|--------------|
| D10 | LoRa NSS |
| D11 | LoRa MOSI |
| D12 | LoRa MISO |
| D13 | LoRa SCK |
| D9 | LoRa RST |
| D8 | LoRa DIO0 |
| D7 | E-ink DC |
| D6 | E-ink CS |
| D5 | E-ink RST |
| D4 | E-ink BUSY |
| D3 | Buzzer |
| D2 | Alert LED |

## Alert Thresholds

| Sensor | Warning | Critical |
|--------|---------|----------|
| Water | Any detection | N/A (always critical) |
| AC Power | N/A | Power loss |
| Temperature | < 40°F or > 90°F | < 32°F or > 100°F |
| Humidity | > 80% | > 95% |
| No signal | > 15 min | > 30 min |

### Alert Behavior

- **Warning:** LED slow blink (1Hz), no sound
- **Critical:** LED fast blink (5Hz), buzzer beeps every 30 sec until acknowledged
- **Water detected or Power loss:** Immediate continuous alarm for 10 seconds, then periodic beeps

## E-ink Display Layout

Normal state (296x128 pixels):
```
┌──────────────────────────────────────┐
│ BASEMENT WATCHDOG      ▲ 12:34 PM   │
├──────────────────────────────────────┤
│  🌡️ 62°F    💧 45%    ☀️ Dark       │
│                                      │
│  🔊 Quiet   ⚡ AC OK   💦 DRY        │
├──────────────────────────────────────┤
│  Status: ALL NORMAL                  │
│  Last update: 2 min ago   🔋 87%    │
└──────────────────────────────────────┘
```

Alert state:
```
┌──────────────────────────────────────┐
│ ⚠️ BASEMENT ALERT ⚠️    12:34 PM    │
├──────────────────────────────────────┤
│         💦 WATER DETECTED!          │
│                                      │
│  🌡️ 62°F    💧 89%    ⚡ AC OK      │
├──────────────────────────────────────┤
│  CHECK SUMP PUMP IMMEDIATELY        │
│  Alert started: 12:31 PM            │
└──────────────────────────────────────┘
```

## Bill of Materials

### Basement Sensor Unit (~$28)

| Component | Search Terms / Specs | ~Price |
|-----------|---------------------|--------|
| Arduino Pro Mini 3.3V 8MHz | "Arduino Pro Mini 3.3V 8MHz ATmega328P" | $4 |
| SX1278 LoRa Module 915MHz | "SX1278 Ra-02 LoRa 915MHz" or "RYLR896" | $5 |
| DHT22 Sensor | "DHT22 AM2302" (with PCB breakout) | $4 |
| Sound Sensor Module | "KY-038 sound sensor module" | $2 |
| LDR Photoresistor | "photoresistor LDR 5mm" | $1 |
| Water Level Sensor | "water level sensor module Arduino" | $2 |
| 18650 Battery + Holder | "18650 battery holder" + "18650 3.7V battery" | $4 |
| TP4056 Charger Module | "TP4056 1A lithium battery charger module" | $1 |
| PC817 Optocoupler | "PC817 optocoupler" | $1 |
| Buzzer | "Active buzzer 5V" | $1 |
| LEDs + Resistors | "5mm LED assortment" + "resistor kit" | $2 |
| Waterproof Junction Box | "waterproof junction box 4x4" or "IP65 project box" | $3 |
| 5V USB Wall Adapter | "5V 1A USB wall charger" | $3 |

### Display Unit (~$25)

| Component | Search Terms / Specs | ~Price |
|-----------|---------------------|--------|
| Arduino Nano | "Arduino Nano V3 ATmega328P CH340" | $4 |
| SX1278 LoRa Module 915MHz | Same as above | $5 |
| 2.9" E-ink Display | "Waveshare 2.9 inch e-ink e-paper SPI" | $12 |
| Buzzer + LED | Same as above | $2 |
| Small Project Box | "ABS project box 100x60x25mm" | $2 |

### Miscellaneous (~$8)

| Item | Notes | ~Price |
|------|-------|--------|
| Jumper wires | "dupont jumper wire kit" | $3 |
| USB cable | Programming + display power | $2 |
| FTDI Adapter | "FTDI FT232RL USB to TTL" (for Pro Mini) | $3 |

**Total Estimated Cost: ~$56**

### Shopping Tips

- Buy Arduino Pro Mini in 3-packs (~$10 for 3) for spares
- LoRa modules often come in 2-packs (~$9 for 2) - perfect
- 915MHz is for US/Canada. Use 868MHz for Europe.

## Software Components

### Sensor Unit Firmware
- Deep sleep with watchdog timer wake
- Sensor reading routines
- LoRa packet transmission
- Local alert logic
- Battery voltage monitoring

### Display Unit Firmware
- LoRa receive with timeout detection
- E-ink display driver (GxEPD2 library)
- Alert state machine
- Time tracking (relative, since no RTC)

### Libraries Required
- RadioLib (LoRa communication)
- DHT sensor library
- GxEPD2 (e-ink display)
- LowPower (sleep modes)

## Testing Strategy

1. **Unit Testing:** Test each sensor individually
2. **LoRa Range Test:** Verify communication through floor
3. **Power Failure Test:** Verify battery backup and AC detection
4. **Water Detection Test:** Test sensor response to water
5. **Alert Test:** Verify buzzer/LED on both units
6. **Endurance Test:** Run for 24+ hours monitoring

## Future Enhancements (Out of Scope)

- WiFi gateway for phone notifications
- Data logging to SD card
- Multiple sensor nodes
- Web dashboard

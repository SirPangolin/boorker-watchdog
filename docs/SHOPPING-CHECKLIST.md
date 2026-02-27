# Basement Watchdog - Amazon Shopping Checklist

**Budget Target:** ~$56
**Date:** 2026-02-26

## How to Use This List

Search each item on Amazon using the **Search Terms** provided. Look for the **Key Specs** to ensure you get the right part. Check items off as you add them to your cart.

---

## Microcontrollers

### [ ] Arduino Pro Mini 3.3V (x2 needed, buy 3-pack)
- **Search:** `Arduino Pro Mini 3.3V 8MHz ATmega328P`
- **Key Specs:**
  - Must be **3.3V** version (NOT 5V)
  - 8MHz clock speed
  - ATmega328P chip
- **Why 3-pack:** Spares are useful, and multipacks are often $10 for 3
- **Budget:** ~$4-5 each or ~$10 for 3

### [ ] Arduino Nano (x1)
- **Search:** `Arduino Nano V3 ATmega328P CH340`
- **Key Specs:**
  - CH340 USB chip (cheaper) or FTDI (more expensive but better)
  - ATmega328P
  - Includes USB cable or buy separately
- **Budget:** ~$4-5

### [ ] FTDI USB-to-Serial Adapter (x1)
- **Search:** `FTDI FT232RL USB to TTL 3.3V 5V`
- **Key Specs:**
  - Has 3.3V/5V jumper or switch
  - 6-pin header that matches Pro Mini
  - DTR pin for auto-reset during upload
- **Note:** Needed to program the Pro Mini (it has no USB)
- **Budget:** ~$3-5

---

## LoRa Radio Modules

### [ ] SX1278 LoRa 915MHz Module (x2)
- **Search:** `SX1278 Ra-02 LoRa 915MHz` OR `RYLR896 LoRa module`
- **Key Specs:**
  - **915MHz for US/Canada** (868MHz for Europe)
  - SX1276 or SX1278 chip
  - SPI interface (NOT UART unless RYLR896)
- **Alternative:** `RYLR896` is easier (UART) but slightly more expensive (~$8 each)
- **Budget:** ~$5 each or ~$9 for 2-pack

---

## Sensors

### [ ] DHT22 Temperature/Humidity Sensor (x1)
- **Search:** `DHT22 AM2302 temperature humidity sensor module`
- **Key Specs:**
  - Get the version with breakout PCB (3 pins: VCC, DATA, GND)
  - More accurate than DHT11
  - Operating range: -40 to 80C, 0-100% RH
- **Budget:** ~$4-5

### [ ] Sound Sensor Module (x1)
- **Search:** `KY-038 sound sensor module Arduino`
- **Key Specs:**
  - Has potentiometer for sensitivity adjustment
  - Both digital and analog output
  - Microphone on board
- **Budget:** ~$2-3

### [ ] LDR Photoresistor (pack)
- **Search:** `photoresistor LDR 5mm 5528`
- **Key Specs:**
  - 5mm diameter (most common)
  - Usually comes in packs of 20-50
- **Note:** You only need 1, but packs are cheap
- **Budget:** ~$1-2 for pack

### [ ] Water Level Sensor (x1)
- **Search:** `water level sensor module Arduino`
- **Key Specs:**
  - Simple probe type with exposed traces
  - Analog output
  - Usually red PCB
- **Budget:** ~$2

---

## Power Components

### [ ] TP4056 Lithium Battery Charger Module (x1 or pack)
- **Search:** `TP4056 1A lithium battery charger module micro USB`
- **Key Specs:**
  - Micro USB input
  - Has battery protection (look for DW01 chip version)
  - Red/blue LED indicators
- **Budget:** ~$1-2 each (often sold in 5-packs for $5)

### [ ] 18650 Battery (x1)
- **Search:** `18650 3.7V rechargeable battery`
- **Key Specs:**
  - 3.7V nominal
  - 2000mAh or higher capacity
  - Flat top or button top (either works)
  - **Reputable brand** (Samsung, LG, Panasonic clones are fine)
- **Warning:** Avoid no-name "9000mAh" claims - those are fake
- **Budget:** ~$3-5

### [ ] 18650 Battery Holder (x1)
- **Search:** `18650 battery holder with wire leads`
- **Key Specs:**
  - Single cell holder
  - With wire leads (not PCB mount)
- **Budget:** ~$1-2

### [ ] 5V USB Wall Adapter (x2)
- **Search:** `5V 1A USB wall charger`
- **Key Specs:**
  - 5V output
  - At least 1A (more is fine)
  - Small/compact preferred
- **Note:** One for basement sensor, one for display
- **Budget:** ~$2-3 each

---

## Display

### [ ] 2.9" E-ink Display (x1)
- **Search:** `Waveshare 2.9 inch e-ink e-paper display SPI` OR `GDEW029T5 e-ink`
- **Key Specs:**
  - 2.9 inch diagonal
  - 296x128 pixels
  - SPI interface
  - Black/White (cheapest) or Black/White/Red
  - **Waveshare brand** is most compatible with libraries
- **Budget:** ~$12-15

---

## Alert Components

### [ ] Active Buzzer 5V (x2 or pack)
- **Search:** `active buzzer 5V Arduino`
- **Key Specs:**
  - **Active** type (makes sound when powered, no PWM needed)
  - 5V operating voltage
  - Usually 12mm diameter
- **Budget:** ~$1-2 for pack of 5

### [ ] PC817 Optocoupler (pack)
- **Search:** `PC817 optocoupler DIP-4`
- **Key Specs:**
  - DIP-4 package
  - For AC power detection circuit
- **Note:** Packs of 10-20 are very cheap
- **Budget:** ~$1-2 for pack

---

## Enclosures

### [ ] Waterproof Junction Box - Basement (x1)
- **Search:** `waterproof junction box IP65 4x4` OR `waterproof project enclosure`
- **Key Specs:**
  - IP65 or higher rating
  - At least 100x100x50mm internal
  - Has cable glands or knockouts
- **Budget:** ~$3-5

### [ ] Small Project Box - Display (x1)
- **Search:** `ABS project box 100x60x25mm` OR `Arduino project enclosure`
- **Key Specs:**
  - Large enough for Nano + e-ink display
  - Can be clear or opaque
  - Consider one with display window cutout
- **Budget:** ~$2-3

---

## Miscellaneous

### [ ] Dupont Jumper Wires (kit)
- **Search:** `dupont jumper wire kit male female`
- **Key Specs:**
  - Male-to-male, male-to-female, female-to-female
  - Various lengths
- **Budget:** ~$3-5 for assortment

### [ ] Resistor Kit (if you don't have one)
- **Search:** `resistor assortment kit 1/4W`
- **Key Specs:**
  - 1/4W rating
  - Range from 10 ohm to 1M ohm
  - Need: 10K (pullup), 220-330 ohm (LEDs), 10K (LDR divider)
- **Budget:** ~$3-5 for kit

### [ ] LED Assortment (if you don't have)
- **Search:** `5mm LED assortment kit`
- **Key Specs:**
  - 5mm diameter
  - Red and/or blue preferred
- **Budget:** ~$2-3 for pack

### [ ] USB Cables (x2)
- **Search:** `micro USB cable short`
- **Key Specs:**
  - Micro USB (for Nano programming and power)
  - 1-3 ft length
- **Budget:** ~$2-3

---

## Shopping Summary

| Category | Items | Est. Cost |
|----------|-------|-----------|
| Microcontrollers | Pro Mini 3-pack, Nano, FTDI | ~$15-18 |
| LoRa Modules | 2x SX1278 915MHz | ~$9-10 |
| Sensors | DHT22, Sound, LDR, Water | ~$9-10 |
| Power | TP4056, 18650, Holder, USB adapters | ~$9-12 |
| Display | 2.9" E-ink | ~$12-15 |
| Alerts | Buzzers, Optocoupler | ~$2-3 |
| Enclosures | Waterproof box, project box | ~$5-8 |
| Misc | Wires, resistors, LEDs, cables | ~$5-10 |

**Estimated Total: $56-76** (depending on pack sizes and sales)

---

## Tips

1. **Check "Frequently Bought Together"** - often shows compatible items
2. **Read recent reviews** - watch for quality issues or changed specs
3. **Prime shipping** - most of these items are available with Prime
4. **Buy multipacks when cheap** - spares are useful for future projects
5. **915MHz, not 868MHz** - for US/Canada operation
6. **3.3V Pro Mini** - NOT the 5V version

---

## Quick Reference - Critical Specs

These are the specs that MUST be correct:

| Item | Critical Spec |
|------|--------------|
| Arduino Pro Mini | **3.3V** version |
| LoRa Module | **915MHz** (US) |
| E-ink Display | **SPI** interface, 2.9" |
| Buzzer | **Active** type |
| DHT | **DHT22** (not DHT11) |

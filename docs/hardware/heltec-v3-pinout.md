# Heltec WiFi LoRa 32 V3 Pinout Reference

Board: **Heltec WiFi LoRa 32 V3 (V3.2)**
Module: **ESP32-S3FN8** (8MB Flash, no PSRAM)
Source: [Official Heltec V3.2 Document (HTIT-WB32LA)](https://resource.heltec.cn/download/WiFi_LoRa_32_V3/HTIT-WB32LA_V3.2.pdf)

## Pin Map

Viewed from the top (OLED side up), USB-C at the bottom. This matches the breadboard orientation.

```
                          (antenna end — top)
                 ┌────────────────────────────────────┐
    Header J3    │    [WiFi]              [LoRa]      │    Header J2
    ─────────    │    antenna             (IPEX)      │    ─────────
>>>  7    18 ○───┤                                    ├───○ 18 19 (USB D-)
>>>  6    17 ○───┤                                    ├───○ 17 20 (USB D+)
>>>  5    16 ○───┤                                    ├───○ 16 21 (OLED RST)
>>>  4    15 ○───┤                                    ├───○ 15 26 ⚠
>>>  3    14 ○───┤                                    ├───○ 14 48
>>>  2    13 ○───┤    ○ LED    ○ CHG                  ├───○ 13 47
      1   12 ○───┤                                    ├───○ 12 33 ⚠
  ⚠  38   11 ○───┤                                    ├───○ 11 34 ⚠
 JTAG 39  10 ○───┤                                    ├───○ 10 35 (LED)
 JTAG 40   9 ○───┤                                    ├───○ 9  36 (Vext ctrl)
 JTAG 41   8 ○───┤                                    ├───○ 8  0  (PRG)
 JTAG 42   7 ○───┤                                    ├───○ 7  RST
  ⚠  45    6 ○───┤    ┌──────────┐                    ├───○ 6  43 (TX)
  ⚠  46    5 ○───┤    │   OLED   │                    ├───○ 5  44 (RX)
  ⚠  37    4 ○───┤    │  128x64  │                    ├───○ 4  Ve (Vext)
     3V3   3 ○───┤    └──────────┘                    ├───○ 3  Ve (Vext)
     3V3   2 ○───┤                                    ├───○ 2  5V
     GND   1 ○───┤      [PRG]  [RST]                 ├───○ 1  GND
                 │         ┌────────┐                 │
                 └─────────┤ USB-C  ├─────────────────┘
                           └────────┘
                       (USB end — bottom)

    >>> = Recommended for external hardware (safe, no conflicts)
    ⚠  = Reserved for SPI Flash/SubSPI or strapping — DO NOT USE
   JTAG = JTAG debug pins — usable if JTAG disabled in software
```

### Breadboard Wiring (Boorker Watchdog)

All signal wires on J3 upper half. Power rails bridged at top of breadboard.
Vext and GND both go to the right rail (J2 side). Bridge carries power to left rail.

```
          ◄── bridge at top of breadboard ──►
    +(left rail)                          +(right rail)
        │                                      │
        │◄────────────── 104 cap ─────────────►│
        │                                      │
        │                  Vext (J2 pin 3/4) ──►│
        │                                      │
        │   DHT22 VCC ◄─┤                      │
        │   SW-420 VCC ◄─┤  (from left rail    │
        │   Buzzer VCC ◄─┘   via bridge)       │
        │                                      │
        ├── DHT22 data ◄── GPIO2  (J3 pin 13)  │
        ├── RGB Blue   ◄── GPIO3  (J3 pin 14)  │
        ├── RGB Red    ◄── GPIO4  (J3 pin 15)  │
        ├── SW-420 DO  ◄── GPIO5  (J3 pin 16)  │
        ├── Buzzer     ◄── GPIO6  (J3 pin 17)  │
        ├── RGB Green  ◄── GPIO7  (J3 pin 18)  │

    -(left rail)                          -(right rail)
        │                                      │
        │◄── bridge at top of breadboard ─────►│
        │                                      │
        │              GND (J2 pin 1, bottom) ─►│
        │              GND (J3 pin 1, bottom) ─►│ (optional second ground)
        │                                      │
        │   RGB LED cathode (long leg) ◄─┤      │
        │                                      │
```

## Header J2 (Right Side)

| Pin | Name | Type | Function |
|-----|------|------|----------|
| 1 | GND | P | Ground |
| 2 | 5V | P | 5V power supply |
| 3 | Ve | P | Vext 3.3V, power for external sensors |
| 4 | Ve | P | Vext 3.3V, power for external sensors |
| 5 | 44 (RX) | I/O | GPIO44, U0RXD, connected to CP2102 TXD |
| 6 | 43 (TX) | I/O | GPIO43, U0TXD, connected to CP2102 RXD |
| 7 | RST | I | CHIP_PU, connected to RST button |
| 8 | 0 | I/O | GPIO0, connected to PRG button |
| 9 | 36 | I/O | GPIO36, Vext control, LED write ctrl |
| 10 | 35 | I/O | GPIO35, white LED |
| 11 | 34 | I/O | GPIO34, SPI Flash — **do not use** |
| 12 | 33 | I/O | GPIO33, SPI Flash — **do not use** |
| 13 | 47 | I/O | GPIO47, general purpose |
| 14 | 48 | I/O | GPIO48, general purpose |
| 15 | 26 | I/O | GPIO26, SubSPI CS — **do not use** |
| 16 | 21 | I/O | GPIO21, OLED RST |
| 17 | 20 | I/O | GPIO20, USB D+ |
| 18 | 19 | I/O | GPIO19, USB D- |

## Header J3 (Left Side)

| Pin | Name | Type | Function |
|-----|------|------|----------|
| 1 | GND | P | Ground |
| 2 | 3V3 | P | 3.3V power supply |
| 3 | 3V3 | P | 3.3V power supply |
| 4 | 37 | I/O | GPIO37, SPI Flash — **do not use** |
| 5 | 46 | I/O | GPIO46, strapping — **avoid** |
| 6 | 45 | I/O | GPIO45, strapping — **avoid** |
| 7 | 42 | I/O | GPIO42, JTAG MTMS |
| 8 | 41 | I/O | GPIO41, JTAG MTDI |
| 9 | 40 | I/O | GPIO40, JTAG MTDO |
| 10 | 39 | I/O | GPIO39, JTAG MTCK |
| 11 | 38 | I/O | GPIO38, SPI Flash — **do not use** |
| 12 | 1 | I/O | GPIO1, ADC1_CH0, battery voltage |
| 13 | 2 | I/O | GPIO2, ADC1_CH1, Touch2 |
| 14 | 3 | I/O | GPIO3, ADC1_CH2, Touch3 |
| 15 | 4 | I/O | GPIO4, ADC1_CH3, Touch4 |
| 16 | 5 | I/O | GPIO5, ADC1_CH4, Touch5 |
| 17 | 6 | I/O | GPIO6, ADC1_CH5, Touch6 |
| 18 | 7 | I/O | GPIO7, ADC1_CH6, Touch7 |

## On-Board Hardware (Reserved GPIOs)

These GPIOs are used by on-board peripherals and must not be used for external wiring.

### LoRa Radio — SX1262 (SPI, active but not on headers)

| GPIO | Function |
|------|----------|
| 8 | NSS (chip select) |
| 9 | SCK (SPI clock) |
| 10 | MOSI (SPI data out) |
| 11 | MISO (SPI data in) |
| 12 | RST (radio reset) |
| 13 | BUSY (radio busy flag) |
| 14 | DIO1 (radio interrupt) |

Note: LoRa GPIOs 8-14 are **not broken out to headers** — they connect directly to the SX1262 on the PCB.

### OLED Display — SSD1306 (I2C, active but not on headers)

| GPIO | Function |
|------|----------|
| 17 | SDA (I2C data) |
| 18 | SCL (I2C clock) |
| 21 | RST (display reset) — on Header J2 pin 16 |

Note: GPIO 17 and 18 are **not broken out to headers**. GPIO 21 is on J2-16 but used by the OLED.

### SPI Flash / SubSPI (on headers but DO NOT USE)

| GPIO | Header Pin | Reason |
|------|-----------|--------|
| 33 | J2-12 | SPI Flash |
| 34 | J2-11 | SPI Flash |
| 37 | J3-4 | SPI Flash |
| 38 | J3-11 | SPI Flash |
| 26 | J2-15 | SubSPI chip select |

### Other On-Board

| GPIO | Function |
|------|----------|
| 0 | PRG button (strapping pin, active LOW) |
| 1 | Battery ADC (resistor divider, multiply by 2) |
| 19 | USB D- (native USB) |
| 20 | USB D+ (native USB) |
| 35 | White LED (active HIGH, on/off or PWM) |
| 36 | Vext control (LOW = external 3.3V on (active LOW, P-channel MOSFET)) |
| 43 | U0TXD (UART0 TX → CP2102 RX) |
| 44 | U0RXD (UART0 RX ← CP2102 TX) |

## Recommended GPIOs for External Hardware

Per [Heltec official guidance](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v3/Pin-diagram-guidance):

| GPIO | Header | Pin | ADC | Touch | Notes |
|------|--------|-----|-----|-------|-------|
| 2 | J3 | 13 | ADC1_CH1 | Yes | |
| 3 | J3 | 14 | ADC1_CH2 | Yes | |
| 4 | J3 | 15 | ADC1_CH3 | Yes | |
| 5 | J3 | 16 | ADC1_CH4 | Yes | |
| 6 | J3 | 17 | ADC1_CH5 | Yes | |
| 7 | J3 | 18 | ADC1_CH6 | Yes | |
| 47 | J2 | 13 | — | No | |
| 48 | J2 | 14 | — | No | |

GPIO 1, 19, 20 are listed as safe by Heltec but have on-board functions (battery ADC, USB). Use only if those functions are not needed.

JTAG pins (39-42) are usable if JTAG is disabled in software, but not recommended for beginners.

## Power Pins

| Pin | Header | Description |
|-----|--------|-------------|
| 3V3 (x2) | J3 pins 2-3 | 3.3V regulated output (main bus — LoRa, OLED, ESP32) |
| 5V | J2 pin 2 | 5V input from USB |
| Vext (x2) | J2 pins 3-4 | 3.3V switched output, controlled by GPIO36 |
| GND | J2 pin 1, J3 pin 1 | Ground reference |

## Power Filtering

Place a **100nF (0.1µF) ceramic decoupling capacitor** (marked "104") across the **Vext (+) and GND (-) breadboard power rails**. Sensors are powered from Vext, not the main 3V3 rail. This isolates sensor current spikes (DHT22, SW-420, buzzer) from the LoRa radio, OLED, and ESP32 core, preventing RF interference and voltage dips during LoRa transmission.

```
+(left rail)   A B C D E     F G H I J   -(right rail)
    │                                         │
    │◄──────────── 104 cap ──────────────────►│
    │                                         │
    │◄── Vext pin (row N, col A/B)            │
    │              GND pin (row M, col I/J) ──►│
```

Sensors tap power from the (+) rail (Vext) and ground from the (-) rail. Firmware must enable Vext at boot — see Vext Power Rail section below.

## Vext Power Rail

The Vext pin provides 3.3V to external sensors, controlled by GPIO36. **Off by default** — firmware must drive GPIO36 LOW to enable (active LOW, P-channel MOSFET).

```c
gpio_set_direction(GPIO_NUM_36, GPIO_MODE_OUTPUT);
gpio_set_level(GPIO_NUM_36, 0);  // LOW = Vext ON — sensors powered
gpio_set_level(GPIO_NUM_36, 1);  // HIGH = Vext OFF — sensors unpowered (deep sleep)
```

Sensors connected to Vext will not work until GPIO36 is driven HIGH.

## LED

White LED on GPIO35. Active HIGH. Not addressable — simple on/off or PWM brightness.

```c
gpio_set_direction(GPIO_NUM_35, GPIO_MODE_OUTPUT);
gpio_set_level(GPIO_NUM_35, 1);  // ON
gpio_set_level(GPIO_NUM_35, 0);  // OFF
```

## USB

Single USB-C connector providing both interfaces:

| Interface | Chip | WSL2 Device | VID:PID |
|-----------|------|-------------|---------|
| UART (recommended) | CP2102 | /dev/ttyUSB0 | 10c4:ea60 |
| Native USB | ESP32-S3 | /dev/ttyACM0 | 303a:1001 |

## Buttons

| Button | GPIO | Function |
|--------|------|----------|
| PRG | 0 | Bootloader entry (hold during reset). Usable as user button after boot. |
| RST | EN | Hardware reset |

## Physical Layout

- **Form factor**: Breadboard compatible (2.54mm spacing, 23mm width)
- **Headers**: 2x 18-pin (J2 right, J3 left)
- **Antenna connectors**: WiFi (PCB spring), LoRa (IPEX — external antenna required for TX)
- **Battery**: JST SH 1.25-2 connector, TP4054 charging IC

## Notes

1. **No PSRAM**: 512KB internal SRAM only. ~379KB free heap at boot.
2. **LoRa GPIO 8-14**: Not on headers — connect directly to SX1262 on PCB.
3. **OLED GPIO 17, 18**: Not on headers — connect directly to SSD1306 on PCB. GPIO21 (OLED RST) is on J2-16.
4. **SPI Flash GPIO 33, 34, 37, 38, 26**: On headers but must not be used — connected to internal flash/SubSPI.
5. **ADC2 + WiFi**: ADC2 channels (GPIO 15-20) unavailable when WiFi is active. Use ADC1 (GPIO 1-7) for analog reads.
6. **Strapping pins**: GPIO 0, 45, 46 affect boot mode — use with caution.
7. **Battery ADC**: GPIO1 reads battery voltage. Formula: `VBAT = 100 / (100+390) * VADC_IN1` (per official doc).

## See Also

- [Component Labels Photo](./heltec-v3-component-labels.webp)
- [Hardware Specs Photo](./heltec-v3-hardware-specs.webp)
- [Power Specs Photo](./heltec-v3-power-specs.webp)
- [Heltec Wiki](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v3/)
- [GPIO Usage Guide](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v3/Pin-diagram-guidance)

Sources:
- [Official V3.2 Document](https://resource.heltec.cn/download/WiFi_LoRa_32_V3/HTIT-WB32LA_V3.2.pdf)
- [Heltec GPIO Usage Guide](https://wiki.heltec.org/docs/devices/open-source-hardware/esp32-series/lora-32/wifi-lora-32-v3/Pin-diagram-guidance)

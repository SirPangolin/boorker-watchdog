# ESP32-S3-DevKitC-1-N32R8V Pinout Reference

Board: **ESP32-S3-DevKitC-1-N32R8V v1.1**
Module: **ESP32-S3-WROOM-2** (32MB Flash, 8MB Octal PSRAM)

## Current Wiring (Boorker Watchdog)

| GPIO | Component | Pin Label | Function |
|------|-----------|-----------|----------|
| 2 | DHT22 | out | Temperature & humidity sensor data |
| 5 | SW-420 | DO | Vibration sensor digital output |
| 6 | Buzzer | I/O | Piezo buzzer signal |
| 7 | RGB LED | Green | External RGB LED green channel |
| 8 | RGB LED | Blue | External RGB LED blue channel |
| 9 | RGB LED | Red | External RGB LED red channel |
| 4 | Float Switch | - | *(future - not wired)* |
| 10 | Button | - | *(future - not wired)* |

## Complete Pinout

### J1 Header (Left Side)

| Pin | Name | Capabilities | Project Use |
|-----|------|--------------|-------------|
| 1 | 3V3 | Power output | 3.3V rail (sensors) |
| 2 | 3V3 | Power output | 3.3V rail |
| 3 | RST | Reset/EN | - |
| 4 | GPIO4 | Touch4, ADC1_CH3, RTC | *Future: Float Switch* |
| 5 | GPIO5 | Touch5, ADC1_CH4, RTC | **SW-420 vibration sensor** |
| 6 | GPIO6 | Touch6, ADC1_CH5, RTC | **Buzzer** |
| 7 | GPIO7 | Touch7, ADC1_CH6, RTC | **RGB LED Green** |
| 8 | GPIO15 | ADC2_CH4, RTC | Available |
| 9 | GPIO16 | ADC2_CH5, RTC | Available |
| 10 | GPIO17 | U1TXD, ADC2_CH6, RTC | Available (UART1 TX) |
| 11 | GPIO18 | U1RXD, ADC2_CH7, RTC | Available (UART1 RX) |
| 12 | GPIO8 | Touch8, ADC1_CH7, RTC | **RGB LED Blue** |
| 13 | GPIO3 | Touch3, ADC1_CH2, RTC | Available |
| 14 | GPIO46 | Strapping | Avoid (boot mode) |
| 15 | GPIO9 | Touch9, ADC1_CH8, RTC | **RGB LED Red** |
| 16 | GPIO10 | Touch10, ADC1_CH9, RTC | *Future: Button* |
| 17 | GPIO11 | Touch11, ADC2_CH0, RTC | Available |
| 18 | GPIO12 | Touch12, ADC2_CH1, RTC | Available |
| 19 | GPIO13 | Touch13, ADC2_CH2, RTC | Available |
| 20 | GPIO14 | Touch14, ADC2_CH3, RTC | Available |
| 21 | 5V | Power input | 5V rail |
| 22 | GND | Ground | Ground rail |

### J3 Header (Right Side)

| Pin | Name | Capabilities | Project Use |
|-----|------|--------------|-------------|
| 1 | GND | Ground | Ground rail |
| 2 | GPIO43 | U0TXD | UART0 TX (serial monitor) |
| 3 | GPIO44 | U0RXD | UART0 RX (serial monitor) |
| 4 | GPIO1 | Touch1, ADC1_CH0, RTC | Available |
| 5 | GPIO2 | Touch2, ADC1_CH1, RTC | **DHT22 data** |
| 6 | GPIO42 | MTMS (JTAG) | Available (JTAG debug) |
| 7 | GPIO41 | MTDI (JTAG) | Available (JTAG debug) |
| 8 | GPIO40 | MTDO (JTAG) | Available (JTAG debug) |
| 9 | GPIO39 | MTCK (JTAG) | Available (JTAG debug) |
| 10 | GPIO38 | RGB LED (built-in) | WS2812 on-board LED |
| 11 | GPIO37 | Reserved | **Do not use** (Octal SPI) |
| 12 | GPIO36 | Reserved | **Do not use** (Octal SPI) |
| 13 | GPIO35 | Reserved | **Do not use** (Octal SPI) |
| 14 | GPIO0 | Strapping (BOOT) | Boot button (avoid) |
| 15 | GPIO45 | Strapping | Avoid (boot mode) |
| 16 | GPIO48 | General GPIO | Available |
| 17 | GPIO47 | General GPIO | Available |
| 18 | GPIO21 | General GPIO, RTC | Available |
| 19 | GPIO20 | USB_D+, ADC2_CH9 | Native USB data+ |
| 20 | GPIO19 | USB_D-, ADC2_CH8 | Native USB data- |
| 21 | GND | Ground | Ground rail |
| 22 | GND | Ground | Ground rail |

## Pin Categories

### Power Pins
- **3V3** (x2): 3.3V regulated output, max ~500mA shared
- **5V**: 5V input from USB
- **GND** (x4): Ground reference

### Reserved/Restricted Pins
| GPIO | Reason | Safe to Use? |
|------|--------|--------------|
| 0 | Strapping (BOOT button) | Input only, with care |
| 35-37 | Octal PSRAM internal SPI | **No** |
| 45, 46 | Strapping pins | Avoid |
| 19, 20 | Native USB | Only if USB not needed |
| 43, 44 | UART0 (serial monitor) | Only if serial not needed |

### Touch-Capable Pins
GPIO 1-14 support capacitive touch sensing (Touch1-Touch14).

### ADC-Capable Pins
- **ADC1**: GPIO 1-10 (always available)
- **ADC2**: GPIO 11-20 (unavailable during WiFi)

### PWM-Capable Pins
All GPIO pins support PWM via LEDC peripheral.

## On-Board Components

| Component | GPIO | Notes |
|-----------|------|-------|
| RGB LED (WS2812) | 38 | v1.1 boards (silkscreen: RGB@IO38) |
| BOOT button | 0 | Hold during reset for download mode |
| RESET button | EN | Hardware reset |
| Power LED (SYS) | - | Always on when powered |

## USB Ports

| Port | Controller | Device | Use |
|------|------------|--------|-----|
| USB (right) | ESP32-S3 native | /dev/ttyACM0 | JTAG, CDC, DFU |
| UART (left) | CP2102N bridge | /dev/ttyUSB0 | Serial, flashing |

## Notes

1. **Octal PSRAM**: GPIO35-37 are internally connected to PSRAM on N32R8V variant
2. **RGB LED version**: v1.1 uses GPIO38, v1.0 used GPIO48
3. **Strapping pins**: GPIO0, 45, 46 affect boot mode - use with caution
4. **ADC2 + WiFi**: ADC2 channels unavailable when WiFi is active
5. **Current limits**: Individual GPIO max 40mA, total chip max 1200mA

## See Also

- [Wiring Diagram](./esp32-s3-devkitc-wiring-pinout.excalidraw) - Visual schematic
- [Espressif User Guide](https://docs.espressif.com/projects/esp-dev-kits/en/latest/esp32s3/esp32-s3-devkitc-1/user_guide_v1.1.html)

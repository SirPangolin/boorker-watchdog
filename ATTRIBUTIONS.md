# Attributions

Boorker Watchdog is built on the work of many open-source projects and contributors. This file acknowledges the third-party code, libraries, and reference implementations used in this project.

> Submodule and vendored dependencies carry their own LICENSE files in-tree. See each component's directory for the authoritative license text.

## ESP-IDF Managed Components

| Component | Author | License | Usage |
|-----------|--------|---------|-------|
| [espressif/led_indicator](https://components.espressif.com/components/espressif/led_indicator) | Espressif Systems | Apache-2.0 (SPDX header) | LED pattern engine for status_led |
| [espressif/led_strip](https://components.espressif.com/components/espressif/led_strip) | Espressif Systems | Apache-2.0 | WS2812 / addressable LED support |
| [espressif/mdns](https://components.espressif.com/components/espressif/mdns) | Espressif Systems | Apache-2.0 | mDNS service discovery |
| [espressif/cmake_utilities](https://components.espressif.com/components/espressif/cmake_utilities) | Espressif Systems | Apache-2.0 | Build system utilities |
| [joltwallet/littlefs](https://components.espressif.com/components/joltwallet/littlefs) | Brian Pugh | MIT | LittleFS filesystem for web SPA storage |
| [nixy4/u8g2](https://components.espressif.com/components/nixy4/u8g2) | olikraus | BSD-2-Clause | SSD1306 OLED display driver (managed component) |
| [suda-morris/am2302_rmt](https://components.espressif.com/components/suda-morris/am2302_rmt) | suda-morris | Apache-2.0 | DHT22/AM2302 temperature sensor via RMT |

## Submodule Dependencies

| Component | Author | License | Location | Usage |
|-----------|--------|---------|----------|-------|
| [MicroLink](https://github.com/CamM2325/microlink) | MicroLink Contributors | MIT | `firmware/components/microlink/` | Tailscale connectivity (disabled in current builds) |
| WireGuard-lwIP | Daniel Hope / [Floorsense](https://www.floorsense.nz) | BSD-3-Clause | `firmware/components/microlink/components/wireguard_lwip/` | WireGuard VPN tunnel, vendored via MicroLink. Originally from [smartalock/wireguard-lwip](https://github.com/smartalock/wireguard-lwip). |
| [u8g2](https://github.com/olikraus/u8g2) | olikraus | BSD-2-Clause | `tools/oled-emulator/u8g2/` | Full source tree for desktop OLED emulator (separate from managed component) |

### Vendored Cryptographic Libraries (via WireGuard-lwIP)

| Library | Author | License | Files |
|---------|--------|---------|-------|
| ChaCha20 | Daniel Hope / Floorsense | BSD-3-Clause | `wireguard_lwip/src/crypto/refc/chacha20.c` |
| Poly1305-donna | [floodyberry](https://github.com/floodyberry/poly1305-donna) | Public Domain / MIT | `wireguard_lwip/src/crypto/refc/poly1305-donna.c` |
| X25519 | Mike Hamburg / Cryptography Research, Inc. | MIT | `wireguard_lwip/src/crypto/refc/x25519.h` |
| BLAKE2s | RFC 7693 reference implementation | CC0 / Public Domain | `wireguard_lwip/src/crypto/refc/blake2s.c` |
| ChaCha20-Poly1305 AEAD | Daniel Hope / Floorsense | BSD-3-Clause | `wireguard_lwip/src/crypto/refc/chacha20poly1305.c` |

## Web Frontend

| Library | Author | License | Usage |
|---------|--------|---------|-------|
| [OAT](https://github.com/knadh/oat) | Kailash Nadh | MIT | Lightweight semantic HTML component library ([oat.ink](https://oat.ink)) |
| [@someshkar/oat-chips](https://github.com/someshkar/oat-chips) | Somesh Kar | MIT | Chip/tag UI components |
| [oat-animate](https://github.com/dharmeshgurnani/oat-animate) | Dharmesh Gurnani | MIT | CSS animation utilities |

## Reference Implementations

Code patterns, register values, or algorithms derived from the following projects:

| Project | Author | License | What was referenced |
|---------|--------|---------|---------------------|
| [RadioLib](https://github.com/jgromes/RadioLib) | Jan Gromeš | MIT | SX1262 PA optimization lookup table (32-entry empirically-tuned power amplifier configuration), PA clamping errata workaround, image calibration frequency pairs, SPI command patterns |
| [Heltec ESP32](https://github.com/HelTecAutomation/Heltec_ESP32) | Heltec Automation | MIT | SX1262 board pin mapping, TCXO configuration values, BUSY pin polling pattern, radio vtable architecture reference |

## Standards and Specifications

| Document | Publisher | Usage |
|----------|-----------|-------|
| SX1261/62 Datasheet (DS.SX1261-2.W.APP Rev 2.1) | Semtech | SPI opcodes, register addresses, IRQ flags, calibration tables, LoRa modulation parameters |
| AN1200.13 (LoRa Modem Design Guide) | Semtech | Time-on-air calculation formula |
| FCC Part 15.247 | FCC | US 915 MHz ISM band regulatory limits |
| EN 300 220 | ETSI | EU 868/433 MHz regulatory limits and duty cycle requirements |
| AS/NZS 4268 | Standards Australia / Standards New Zealand | AU 915 MHz regulatory limits and 400ms dwell time |

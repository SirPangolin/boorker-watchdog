# Boorker Watchdog

A self-hosted IoT sensor mesh platform built on ESP32-S3. Monitor your home infrastructure with no cloud dependency.

## Quick Start

**Prerequisites:** [ESP-IDF v5.5+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/), [Node.js](https://nodejs.org/), ESP32-S3 board (Heltec WiFi LoRa V3 or DevKitC-1)

```bash
cd www && npm install && npm run build && bash scripts/deploy.sh && cd ..
cd firmware && source ~/esp/esp-idf/export.sh && idf.py build && idf.py -p /dev/ttyUSB0 flash
```

On first boot the device generates unique secrets, enters BLE provisioning mode, and displays setup info on the OLED and serial console.

## Tools

- **Web UI:** `cd www && npm run dev` — Vite dev server with mock API
- **OLED Emulator:** `cd tools/oled-emulator && make run` — desktop screen preview (requires `libsdl2-dev`)
- **Console:** UART at 115200 baud, type `help`

## License

MIT

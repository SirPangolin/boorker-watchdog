# Boorker Watchdog

A self-hosted IoT sensor mesh platform built on ESP32-S3. Monitor your home infrastructure — sump pumps, temperature, humidity, vibration — with no cloud dependency.

## Quick Start

### Prerequisites

- [ESP-IDF v5.5+](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/get-started/)
- [Node.js](https://nodejs.org/) (for web UI development)
- ESP32-S3 development board (DevKitC-1 or Heltec WiFi LoRa V3)

### Build & Flash

```bash
# Build web UI
cd www && npm install && npm run build && bash scripts/deploy.sh && cd ..

# Build and flash firmware
cd firmware
source ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyUSB0 flash
```

### First Boot

1. Device generates unique credentials and enters BLE provisioning mode
2. Use the [ESP BLE Provisioning](https://play.google.com/store/apps/details?id=com.espressif.provble) app to configure WiFi
3. Navigate to `http://boorker-XXXX.local` and sign in with the generated password (shown on serial console)
4. Change your password when prompted

### Web UI Development

```bash
cd www
npm run dev    # Starts Vite dev server with mock API at http://localhost:5173
```

The mock middleware serves JSON fixtures from `www/mock/`, so the entire UI can be developed without hardware connected.

## API

Base URL: `http://boorker-XXXX.local/api/v1`

All endpoints (except `/auth/login` and `/auth/status`) require session authentication.

| Method | Endpoint | Description |
|--------|----------|-------------|
| POST | `/auth/login` | Authenticate |
| POST | `/auth/logout` | End session |
| GET | `/auth/status` | Check auth state |
| PUT | `/auth/password` | Change password |
| GET | `/system/status` | Device status, WiFi, memory |
| GET | `/system/info` | Firmware version, chip info |
| POST | `/system/reboot` | Schedule reboot |
| POST | `/system/factory-reset` | Factory reset |
| GET | `/system/motd` | Active notifications |
| DELETE | `/system/motd` | Dismiss notification |
| GET | `/ota` | OTA update status |
| GET | `/ota?refresh=true` | Check GitHub for updates |
| PUT | `/ota` | Install update from GitHub |
| POST | `/ota` | Upload firmware binary |
| DELETE | `/ota` | Abort in-progress update |

## Console

UART at 115200 baud. Type `help` for available commands.

```
boorker> status
Boorker v0.7.1 - boorker-727C
Uptime: 0h 18m 49s
WiFi: CONNECTED (192.168.x.x)
```

## Security

- Hardware RNG for all credentials (unique per device)
- SHA-256 password hashing with salt
- Session-based auth with configurable expiry
- Brute force lockout (5 attempts, 5 min cooldown, persists across reboots)
- Forced password change on first login

## Hardware

| Target | Board | Notes |
|--------|-------|-------|
| Development | ESP32-S3-DevKitC-1-N32R8V | 8MB PSRAM, 32MB Flash |
| Production | Heltec WiFi LoRa 32 V3 | LoRa, OLED, battery |

See `docs/hardware/` for pinout references and wiring diagrams.

## License

MIT

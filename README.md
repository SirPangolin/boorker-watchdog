# Boorker Watchdog

ESP32-S3 based network monitoring device with WiFi provisioning, web interface, and optional Tailscale VPN support.

## Features

- **BLE WiFi Provisioning**: First-boot setup via Espressif BLE Provisioning app
- **Web Interface**: Static web UI served from LittleFS with session-based auth
- **REST API**: JSON API for system monitoring and control
- **Console Commands**: UART console for debugging and administration
- **Tailscale VPN**: Optional Tailscale integration (disabled by default)
- **mDNS Discovery**: Device advertised as `boorker-XXXX.local`

## Hardware

- **MCU**: ESP32-S3-DevKitC-1-N32R8V
- **RAM**: 8MB PSRAM
- **Flash**: 32MB (8MB app, 1.5MB storage partition)

## Building

```bash
cd firmware
source ~/esp/esp-idf/export.sh
idf.py build
idf.py -p /dev/ttyUSB0 flash monitor
```

## First Boot

On first boot, the device:
1. Generates unique credentials (web password, AP password, BLE PIN)
2. Enters BLE provisioning mode
3. Advertises as `BOORKER-XXXX` for WiFi setup

Use the [ESP BLE Provisioning](https://play.google.com/store/apps/details?id=com.espressif.provble) app to configure WiFi.

## Console Commands

Access via UART at 115200 baud. Prompt: `boorker>`

| Command | Description |
|---------|-------------|
| `help` | List all commands |
| `version` | Show firmware version and chip info |
| `status` | Show system status (WiFi, memory, uptime) |
| `free` | Show memory statistics (heap, PSRAM) |
| `uptime` | Show device uptime |
| `reboot [now\|N\|cancel]` | Reboot device (default: 3s delay) |

### Reboot Examples

```bash
reboot          # Reboot in 3 seconds (default)
reboot now      # Immediate reboot
reboot 10       # Reboot in 10 seconds
reboot cancel   # Cancel pending reboot
```

## REST API

Base URL: `http://boorker-XXXX.local` or device IP

### Authentication

Session-based authentication with cookies. Login required for most endpoints.

#### POST /api/v1/auth/login
```json
// Request
{"username": "admin", "password": "<device_password>"}

// Response
{"success": true}
// Sets session cookie
```

#### POST /api/v1/auth/logout
```json
// Response
{"success": true}
```

#### GET /api/v1/auth/status
No auth required - used to check authentication state.
```json
{"authenticated": true, "password_changed": false}
```

#### PUT /api/v1/auth/password
```json
// Request
{"current_password": "...", "new_password": "..."}

// Response
{"success": true}
```

### System Endpoints

All require authentication.

#### GET /api/v1/system/status
```json
{
  "uptime": 3661,
  "heap_free": 123456,
  "psram_free": 8388608,
  "node_name": "boorker-A1B2",
  "power": {"source": "unknown", "ac_present": true}
}
```

#### GET /api/v1/system/info
```json
{
  "version": "0.2.0",
  "idf_version": "v5.5-...",
  "chip_revision": 0,
  "cores": 2,
  "mac": "AA:BB:CC:DD:EE:FF"
}
```

#### POST /api/v1/system/reboot
Schedule a device reboot.
```json
// Optional request body
{"delay": 5}

// Response
{"success": true, "delay": 5}
```

#### DELETE /api/v1/system/reboot
Cancel a pending reboot.
```json
// Response (success)
{"success": true, "message": "Reboot cancelled"}

// Response (no reboot pending)
{"error": true, "message": "No reboot pending"}
```

#### POST /api/v1/system/factory-reset
Resets device credentials and reboots.
```json
{"success": true, "message": "Factory reset complete, rebooting..."}
```

#### GET /api/v1/system/qr
Returns QR code JSON for device setup. **Only available during first boot** (returns 403 after credentials are acknowledged).
```json
{
  "name": "boorker-A1B2",
  "web_pass": "...",
  "ap_pass": "...",
  "ble_pop": "123456",
  "ble_name": "PROV_A1B2",
  "setup_url": "http://192.168.4.1"
}
```

## Security

- **Session tokens**: 32-character random tokens with 1-hour expiry
- **Password hashing**: SHA-256 with random salt (stored in NVS)
- **Brute force protection**: Account lockout after 5 failed attempts (5 min)
- **Path traversal protection**: URL-encoded attacks blocked
- **Constant-time comparison**: Prevents timing attacks on auth

## Configuration

### Kconfig Options

```
CONFIG_WEB_SERVER_PORT=80              # HTTP port
CONFIG_WEB_SERVER_MAX_URI_LEN=64       # Max URI length
CONFIG_SYSTEM_CONSOLE_REBOOT_DEFAULT_DELAY=3
CONFIG_SYSTEM_CONSOLE_REBOOT_MAX_DELAY=30
CONFIG_TS_MGR_ENABLED=n                # Tailscale (disabled by default)
```

### Enabling Tailscale

```bash
idf.py menuconfig
# Navigate to: Component config > Tailscale Manager
# Enable CONFIG_TS_MGR_ENABLED
```

## License

MIT

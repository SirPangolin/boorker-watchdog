# Boorker Web Interface - System Design

**Date:** 2026-02-28
**Status:** Approved
**Phase:** 4
**Depends On:** Phase 1 (Project Structure), Phase 2 (WiFi Manager), Phase 3 (Tailscale Manager)

---

## Overview

A self-contained web interface served by each Boorker node, accessible via local WiFi, AP mode (setup/fallback), or Tailscale VPN. Designed for the memory-constrained Heltec WiFi LoRa 32 V3 (512KB SRAM, no PSRAM).

**Key Capabilities:**
- Status dashboard with real-time updates via WebSocket
- Device configuration (WiFi, Tailscale, sensors, rules)
- Non-root terminal interface for diagnostics
- OTA firmware updates via web upload
- Mesh peer visibility
- Secure authentication with per-device unique credentials
- Adaptive light/dark theme (GitLab-style)

---

## Hardware Constraints

### Target: Heltec WiFi LoRa 32 V3.2

| Resource | Available | Notes |
|----------|-----------|-------|
| SRAM | 512KB | Working memory (no PSRAM) |
| Flash | 8MB | Firmware + LittleFS storage |
| CPU | ESP32-S3 @ 240MHz | Dual-core |

### Memory Budget

| Component | SRAM Usage |
|-----------|------------|
| Core (FreeRTOS, WiFi, Tailscale, LoRa) | ~283KB |
| Sensors, Rules, Alerts | ~16KB |
| Display, Buzzer, Button | ~6KB |
| Mesh peer registry (8 nodes) | ~3KB |
| Web server + WebSocket (2 conn) | ~20KB |
| Terminal command handler | ~4KB |
| Storage buffers (NVS, LittleFS, logs) | ~10KB |
| OTA streaming | ~5KB |
| **Total** | **~347KB** |
| **Headroom** | **~165KB** |

---

## Architecture

### Network Modes

All modes serve the same web interface:

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│    AP Mode      │────►│    STA Mode     │────►│   Tailscale     │
│  (Setup/Reset)  │     │    (Normal)     │     │    (Remote)     │
│  192.168.4.1    │     │   DHCP IP       │     │   100.x.y.z     │
└─────────────────┘     └─────────────────┘     └─────────────────┘
```

**AP Mode Triggers:**
- First boot (no WiFi credentials)
- WiFi connection fails 3x consecutively
- Long button press (10 seconds)

### Application Stack

```
┌─────────────────────────────────────────────────────────────────┐
│                        HELTEC V3 NODE                           │
├─────────────────────────────────────────────────────────────────┤
│  ┌─────────────┐     ┌─────────────┐     ┌─────────────┐       │
│  │   LittleFS  │     │ HTTP Server │     │  WebSocket  │       │
│  │   (Flash)   │────►│  (esp_http) │◄───►│   Server    │       │
│  │   ~11KB     │     │  REST JSON  │     │  Real-time  │       │
│  └─────────────┘     └──────┬──────┘     └──────┬──────┘       │
│                             │                   │               │
│                             ▼                   ▼               │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                    Application Layer                      │  │
│  │  ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────────────┐ │  │
│  │  │ Sensors │ │  Rules  │ │ Alerts  │ │   Mesh Peers    │ │  │
│  │  │ Registry│ │ Engine  │ │ Manager │ │   Registry      │ │  │
│  │  └─────────┘ └─────────┘ └─────────┘ └─────────────────┘ │  │
│  └──────────────────────────────────────────────────────────┘  │
│                                                                 │
│  ┌──────────────────────────────────────────────────────────┐  │
│  │                    Hardware Layer                         │  │
│  │  WiFi │ Tailscale │ LoRa │ OLED │ Buzzer │ Button │ NVS  │  │
│  └───────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Device Identity & Provisioning

### Unique Per-Device Credentials

Credentials generated using ESP32 hardware RNG at first boot (not derivable from MAC):

| Item | Source | Example |
|------|--------|---------|
| Node name | MAC-derived (discoverable) | `boorker-E4F2` |
| Web password | Hardware RNG, 12 chars | `xK9#mP2$vL4n` |
| AP password | Hardware RNG, 12 chars | `Qw8$nR3@jT6x` |
| BLE PoP PIN | Hardware RNG, 6 digits | `847291` |
| mDNS hostname | MAC-derived | `boorker-e4f2.local` |

### First Boot Flow

```
1. ESP32 hardware RNG generates unique credentials
2. Credentials stored in encrypted NVS
3. OLED displays credentials + QR code
4. User photographs QR or writes down credentials
5. Press button to acknowledge → normal boot continues
```

### QR Code Contents

```json
{
  "name": "boorker-E4F2",
  "web_pass": "xK9#mP2$vL4n",
  "ap_pass": "Qw8$nR3@jT6x",
  "ble_pop": "847291",
  "ble_name": "PROV_E4F2",
  "setup_url": "http://192.168.4.1"
}
```

QR displayed on OLED at first boot only - requires physical access.

### Recovery

| Scenario | Solution |
|----------|----------|
| Forgot password | Factory reset (15s button hold) → new credentials |
| Lost QR code | Factory reset → new QR shown on OLED |
| Remote lockout | Physical access required (by design) |

---

## Security

### Storage Encryption

- **NVS Partition:** AES-256 encrypted (ESP-IDF flash encryption)
- **Encryption key:** Stored in eFuse (tamper-resistant)
- All credentials, config stored in encrypted NVS

### Authentication

```
┌─────────────────────────────────────────────────────────────┐
│  Public (no auth):                                          │
│  ├── GET  /login              Login page                    │
│  ├── POST /api/v1/auth/login  Authenticate                  │
│  └── GET  /api/v1/health      Basic health check            │
│                                                             │
│  Protected (session or basic auth):                         │
│  ├── GET  /*                  All pages                     │
│  ├── *    /api/v1/*           All API endpoints             │
│  └── WS   /ws                 WebSocket                     │
└─────────────────────────────────────────────────────────────┘
```

**Auth Methods:**
- Cookie: `session=<token>` (browser)
- Header: `Authorization: Basic <base64>` (API clients)

### Security Features

| Feature | Implementation |
|---------|----------------|
| Unique defaults | Hardware RNG per device |
| Forced password change | Required on first web login |
| Rate limiting | 5 failed logins → 5 min lockout |
| Password hashing | SHA-256 + salt in NVS |
| No password exposure | API never returns actual passwords |
| Session expiry | Configurable, default 1 hour |

---

## Frontend

### Technology Choices

| Aspect | Choice | Rationale |
|--------|--------|-----------|
| Framework | Vanilla JS | No dependencies, minimal size |
| CSS | Vanilla + CSS variables | Adaptive theming, no build |
| Real-time | WebSocket | Efficient, bidirectional |
| Routing | Hash-based SPA | No server config needed |
| Build | Minify + Gzip | ~80% size reduction |

### Asset Optimization

| File | Original | Gzipped |
|------|----------|---------|
| `app.js` | 43KB | 8KB |
| `app.css` | 8KB | 2KB |
| `index.html` | 3KB | 0.8KB |
| `login.html` | 2KB | 0.5KB |
| **Total** | 56KB | **~11KB** |

### File Structure (LittleFS)

```
/littlefs/
├── index.html.gz        (0.8KB)
├── login.html.gz        (0.5KB)
├── css/
│   └── app.css.gz       (2KB)
└── js/
    └── app.js.gz        (8KB)
```

### Theme (Adaptive, GitLab-style)

```css
:root {
  --bg-primary: #ffffff;
  --bg-secondary: #f5f5f5;
  --text-primary: #333333;
  --accent: #6b4fbb;
  --success: #108548;
  --warning: #c17d10;
  --danger: #dd2b0e;
}

@media (prefers-color-scheme: dark) {
  :root {
    --bg-primary: #1f1f1f;
    --bg-secondary: #2d2d2d;
    --text-primary: #fafafa;
    --accent: #9b8aff;
  }
}
```

---

## Navigation & Pages

### Layout

```
┌───────────────────────────────────────────────────────────┐
│ ☰ boorker-E4F2                    ● Connected   [👤]     │
├───────────┬───────────────────────────────────────────────┤
│           │                                               │
│  Dashboard│              MAIN CONTENT                     │
│  Terminal │                                               │
│  Settings │                                               │
│  Update   │                                               │
│  ───────  │                                               │
│  Sensors  │                                               │
│  Rules    │                                               │
│  Alerts   │                                               │
│  Mesh     │                                               │
│  Logs     │                                               │
│           │                                               │
├───────────┴───────────────────────────────────────────────┤
│ v0.3.0 │ WiFi: 192.168.1.100 │ TS: 100.73.37.117         │
└───────────────────────────────────────────────────────────┘
```

### Pages

| Page | Route | Phase | Purpose |
|------|-------|-------|---------|
| Dashboard | `#/` | MVP | Status, quick stats, active alerts, peers |
| Terminal | `#/terminal` | MVP | Non-root command interface |
| Settings | `#/settings` | MVP | WiFi, Tailscale, device, security config |
| Update | `#/update` | MVP | OTA firmware upload |
| Sensors | `#/sensors` | Future | Sensor list, readings, configuration |
| Rules | `#/rules` | Future | Alert rules CRUD |
| Alerts | `#/alerts` | Future | Alert history, acknowledge |
| Mesh | `#/mesh` | Future | Peer details, topology |
| Logs | `#/logs` | Future | Log viewer, export, webhook config |

---

## Web Terminal

Non-root command interface over WebSocket:

### Available Commands

| Command | Description |
|---------|-------------|
| `help` | List commands |
| `status` | System overview |
| `sensors` | Current readings |
| `peers` | Mesh node list |
| `logs [n]` | Last n log entries |
| `ping <peer>` | Ping mesh peer |
| `wifi status` | WiFi details |
| `tailscale status` | VPN status |
| `version` | Firmware info |
| `free` | Memory stats |
| `tasks` | FreeRTOS task list |
| `uptime` | System uptime |
| `reboot` | Reboot (requires confirmation) |

### Excluded (Root-Only via Serial)

- `nvs erase` - Destructive
- `factory-reset` - Use web UI with confirmation
- `flash` - OTA only via web upload
- Credential changes - Use web UI

---

## API Specification

### Base URL

```
http://<device-ip>/api/v1
```

### Authentication

| Method | Endpoint | Purpose |
|--------|----------|---------|
| `POST` | `/auth/login` | Login, returns session token |
| `POST` | `/auth/logout` | Logout, clears session |
| `GET` | `/auth/status` | Check auth status |
| `PUT` | `/auth/password` | Change password |

### System

| Method | Endpoint | Purpose |
|--------|----------|---------|
| `GET` | `/system/status` | Uptime, heap, version, power status |
| `GET` | `/system/info` | MAC, chip info, flash size |
| `POST` | `/system/reboot` | Reboot device |
| `POST` | `/system/factory-reset` | Clear NVS, reboot |
| `GET` | `/system/qr` | Regenerate provisioning QR |

### Power Status Response

```json
{
  "uptime": 123456,
  "heap_free": 165000,
  "version": "0.3.0",
  "power": {
    "source": "ac",
    "ac_present": true,
    "battery_percent": 87,
    "battery_voltage": 3.92,
    "battery_charging": true
  }
}
```

### WiFi

| Method | Endpoint | Purpose |
|--------|----------|---------|
| `GET` | `/wifi/status` | Connection status, IP, RSSI |
| `GET` | `/wifi/scan` | Scan available networks |
| `PUT` | `/wifi/config` | Set SSID/password |
| `POST` | `/wifi/reconnect` | Force reconnection |
| `DELETE` | `/wifi/config` | Clear credentials, enter AP mode |

### Tailscale

| Method | Endpoint | Purpose |
|--------|----------|---------|
| `GET` | `/tailscale/status` | Connection status, VPN IP |
| `PUT` | `/tailscale/config` | Set auth key, hostname |
| `POST` | `/tailscale/reconnect` | Force reconnection |
| `DELETE` | `/tailscale/config` | Disable Tailscale |

### Device Config

| Method | Endpoint | Purpose |
|--------|----------|---------|
| `GET/PUT` | `/config/device` | Name, role, timezone |
| `GET/PUT` | `/config/display` | Brightness, timeout |
| `GET/PUT` | `/config/logging` | Webhook URL, intervals |

### Sensors (Future)

| Method | Endpoint | Purpose |
|--------|----------|---------|
| `GET` | `/sensors` | List all sensors + readings |
| `GET` | `/sensors/:id` | Single sensor details |
| `PUT` | `/sensors/:id/config` | Configure sensor |

### Rules

| Method | Endpoint | Purpose |
|--------|----------|---------|
| `GET` | `/rules` | List all rules |
| `GET` | `/rules/:id` | Single rule details |
| `POST` | `/rules` | Create user rule |
| `PUT` | `/rules/:id` | Update rule |
| `DELETE` | `/rules/:id` | Delete rule (user rules only) |

### Alerts (Future)

| Method | Endpoint | Purpose |
|--------|----------|---------|
| `GET` | `/alerts` | List alerts |
| `GET` | `/alerts/active` | Active alerts only |
| `POST` | `/alerts/:id/ack` | Acknowledge alert |
| `POST` | `/alerts/:id/silence` | Silence for duration |

### Mesh

| Method | Endpoint | Purpose |
|--------|----------|---------|
| `GET` | `/mesh/peers` | List all peers |
| `GET` | `/mesh/peers/:name` | Peer details + readings |
| `POST` | `/mesh/ping/:name` | Ping peer |

### Logs

| Method | Endpoint | Purpose |
|--------|----------|---------|
| `GET` | `/logs` | Recent log entries |
| `GET` | `/logs/export` | CSV download |
| `DELETE` | `/logs` | Clear log buffer |

### OTA

| Method | Endpoint | Purpose |
|--------|----------|---------|
| `GET` | `/ota/status` | Current version |
| `POST` | `/ota/upload` | Upload firmware binary |
| `GET` | `/ota/progress` | Upload progress |

### Terminal

| Protocol | Endpoint | Purpose |
|----------|----------|---------|
| WebSocket | `/terminal` | Bidirectional command interface |

---

## WebSocket Events

### Connection

```
WS /ws
```

### Subscribe/Unsubscribe

```json
{"type": "subscribe", "topics": ["status", "alerts", "peers", "sensors"]}
{"type": "unsubscribe", "topics": ["sensors"]}
```

### Server → Client Events

```json
{"type": "status", "data": {"heap_free": 165000, "uptime": 123456}}
{"type": "power", "data": {"source": "battery", "ac_present": false, "battery_percent": 87}}
{"type": "alert", "data": {"id": 1, "rule": "water_alert", "message": "Water detected!"}}
{"type": "peer_update", "data": {"name": "boorker-A3B1", "status": "online", "rssi": -45}}
{"type": "sensor", "data": {"id": "temp", "value": 68.5, "unit": "°F"}}
```

---

## Factory Default Rules

Pre-installed system rules (protected, cannot be deleted):

| Rule ID | Name | Condition | Severity |
|---------|------|-----------|----------|
| `_sys_ac_lost` | AC Power Lost | `power.ac_present == false` | Critical |
| `_sys_ac_restored` | AC Power Restored | `power.ac_present == true` (after loss) | Info |
| `_sys_battery_low` | Battery Low | `power.battery_percent < 20` | Warning |
| `_sys_battery_critical` | Battery Critical | `power.battery_percent < 10` | Critical |
| `_sys_peer_lost` | Mesh Peer Offline | `peer.last_seen > 15min` | Warning |
| `_sys_wifi_lost` | WiFi Disconnected | `wifi.connected == false` for 5min | Warning |
| `_sys_tailscale_lost` | Tailscale Disconnected | `tailscale.connected == false` for 10min | Warning |
| `_sys_heap_low` | Memory Low | `system.heap_free < 20KB` | Warning |

### Default Actions by Severity

| Severity | Actions |
|----------|---------|
| Critical | Buzzer, OLED alert, mesh broadcast, webhook |
| Warning | OLED warning, webhook |
| Info | Log only |

### Rule Protection

- System rules (`_sys_*`): Can configure actions/backoff, cannot delete
- User rules: Full CRUD access

---

## Features Summary

### Included

| Feature | Notes |
|---------|-------|
| Status dashboard | Real-time via WebSocket |
| WiFi configuration | Scan, connect, AP fallback |
| Tailscale configuration | Auth key, hostname |
| Device settings | Name, role, timezone, display |
| Web terminal | Non-root diagnostics |
| OTA updates | Streaming upload, rollback |
| Mesh peer visibility | Current state, readings |
| Factory default rules | Power, connectivity alerts |
| Secure authentication | Per-device unique credentials |
| Adaptive theme | Light/dark based on system |

### Excluded (Memory/Complexity)

| Feature | Alternative |
|---------|-------------|
| Virtual OLED canvas | Use physical OLED |
| In-browser charts | Export CSV, view externally |
| Extensive log history | Ring buffer + webhook push |

---

## Implementation Notes

### ESP-IDF Components Required

- `esp_http_server` - HTTP/WebSocket server
- `esp_https_server` - Optional HTTPS (future)
- `cJSON` - JSON serialization
- `nvs_flash` - Encrypted config storage
- `esp_littlefs` - Web file storage

### Gzip Serving

```c
if (strstr(req->headers, "Accept-Encoding: gzip")) {
    httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    serve_file(req, "/littlefs/js/app.js.gz");
}
```

### Build Tooling

```bash
# Minify + gzip (dev machine)
npx esbuild js/*.js --bundle --minify --outfile=dist/js/app.min.js
npx esbuild css/*.css --minify --outfile=dist/css/app.min.css
gzip -k dist/js/*.js dist/css/*.css dist/*.html

# Upload to device
python littlefs_upload.py dist/ /dev/ttyUSB0
```

---

## Next Steps

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:writing-plans to create implementation plan.

1. Create `web_server` component with HTTP + WebSocket
2. Create `web_auth` module for session management
3. Create `device_identity` module for credential generation
4. Implement REST API endpoints
5. Build frontend SPA
6. Integrate with existing wifi_manager and tailscale_manager
7. Add factory default rules to rules engine

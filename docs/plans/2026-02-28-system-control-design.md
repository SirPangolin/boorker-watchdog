# System Control - Design Document

**Date:** 2026-02-28
**Status:** Approved
**Phase:** 5A
**Depends On:** Phase 4 (Web Interface MVP)

---

## Overview

System control functionality providing both console commands (serial terminal) and REST API endpoints for device management. Follows Linux conventions where appropriate (e.g., `shutdown -r` pattern for reboot).

**Key Capabilities:**
- Reboot with configurable delay and cancellation
- System status (uptime, memory, version)
- Factory reset via web UI (credentials regeneration)
- Password management
- Session status checking

---

## Device Agnosticism

The implementation must work across different ESP32 variants:

| Device | SRAM | PSRAM | Notes |
|--------|------|-------|-------|
| ESP32-S3-DevKitC-1-N32R8V | 512KB | 8MB | Development board |
| Heltec WiFi LoRa 32 V3 | 512KB | None | Production target |

**Implementation Rules:**
- Check PSRAM availability before reporting: `heap_caps_get_free_size(MALLOC_CAP_SPIRAM)`
- Return 0 or omit field if capability not present
- Never fail/crash due to missing optional hardware
- Use capability detection, not compile-time board selection

---

## Console Commands

Registered via `esp_console` framework, following existing `tailscale_console.c` pattern.

### Command Reference

| Command | Arguments | Description |
|---------|-----------|-------------|
| `reboot` | `[now\|<seconds>\|cancel]` | Reboot with countdown |
| `version` | None | Show firmware version |
| `free` | None | Memory statistics |
| `uptime` | None | System uptime |
| `status` | None | Combined system overview |

### Reboot Command

Follows Linux `shutdown -r` pattern:

```
boorker> reboot           # Reboot in 3 seconds (default)
Rebooting in 3... 2... 1...
Restarting now.

boorker> reboot 10        # Reboot in 10 seconds
Reboot scheduled in 10 seconds. Use 'reboot cancel' to abort.
Rebooting in 10... 9... 8...

boorker> reboot cancel    # Cancel pending reboot
Reboot cancelled.

boorker> reboot now       # Immediate reboot
Restarting now.
```

**Implementation:**
- Uses FreeRTOS one-shot timer for countdown
- Prints countdown to console each second
- `cancel` deletes the timer if pending
- Shared logic between console and API
- Calls `esp_restart()` when timer fires

### Version Command

```
boorker> version
Boorker v0.2.0
ESP-IDF v5.5.3
Chip: ESP32-S3 rev3, 2 cores
```

### Free Command

```
boorker> free
Memory:
  Heap:  165,432 bytes free (min: 142,108)
  PSRAM: 8,388,608 bytes free
```

If no PSRAM:
```
boorker> free
Memory:
  Heap: 165,432 bytes free (min: 142,108)
```

### Uptime Command

```
boorker> uptime
Uptime: 2h 34m 12s
```

### Status Command

Combined overview (calls other functions internally):

```
boorker> status
Boorker v0.2.0 - boorker-727C
Uptime: 2h 34m 12s
Memory:
  Heap:  165,432 bytes free
  PSRAM: 8,388,608 bytes free
WiFi: connected (192.168.68.53, RSSI: -45 dBm)
Tailscale: connected (100.73.37.117)
```

---

## REST API Endpoints

All endpoints under `/api/v1/` prefix, requiring authentication unless noted.

### Authentication Endpoints

#### GET /api/v1/auth/status

Check if current session is valid.

**Response (200):**
```json
{
  "authenticated": true,
  "username": "admin"
}
```

**Response (401):**
```json
{
  "authenticated": false
}
```

#### PUT /api/v1/auth/password

Change web password.

**Request:**
```json
{
  "current_password": "oldpass",
  "new_password": "newpass123"
}
```

**Response (200):**
```json
{
  "success": true,
  "message": "Password changed. Please log in again."
}
```

**Response (400):**
```json
{
  "error": true,
  "message": "Current password incorrect"
}
```

**Behavior:**
- Validates current password first
- Updates password hash in NVS via `web_auth_set_password()`
- Invalidates all existing sessions
- Frontend should redirect to login

### System Endpoints

#### POST /api/v1/system/reboot

Trigger device reboot.

**Request:**
```json
{
  "delay": 3
}
```

- `delay`: Seconds before reboot (0 = immediate, default 3, max 300)

**Response (200):**
```json
{
  "success": true,
  "message": "Rebooting in 3 seconds",
  "delay": 3
}
```

#### DELETE /api/v1/system/reboot

Cancel pending reboot.

**Response (200):**
```json
{
  "success": true,
  "message": "Reboot cancelled"
}
```

**Response (400):**
```json
{
  "error": true,
  "message": "No reboot pending"
}
```

#### POST /api/v1/system/factory-reset

Factory reset device (web UI only, not console).

**Request:**
```json
{
  "confirm": true
}
```

**Response (200):**
```json
{
  "success": true,
  "message": "Factory reset initiated. Device will reboot with new credentials."
}
```

**Behavior:**
1. Requires `confirm: true` in body
2. Calls `device_identity_regenerate()` to clear and regenerate credentials
3. Reboots device after 2 seconds
4. New credentials shown on serial console at boot

#### GET /api/v1/system/qr

Get provisioning QR code data.

**Response (200):**
```json
{
  "name": "boorker-727C",
  "web_pass": "Ee58Jzfnwnmx",
  "ap_pass": "V9w@HdeS9!vb",
  "ble_pop": "280168",
  "ble_name": "PROV_727C",
  "setup_url": "http://192.168.4.1"
}
```

**Note:** Only accessible on first boot or after acknowledging credentials. Returns 403 if credentials already acknowledged.

### Enhanced Existing Endpoints

#### GET /api/v1/system/info (enhance)

Add version information:

```json
{
  "chip_revision": 3,
  "cores": 2,
  "mac": "34:85:18:A1:72:7C",
  "version": "0.2.0",
  "idf_version": "5.5.3"
}
```

#### GET /api/v1/system/status (enhance)

Add PSRAM (if available):

```json
{
  "uptime": 9252,
  "heap_free": 165432,
  "heap_min": 142108,
  "psram_free": 8388608,
  "node_name": "boorker-727C",
  "power": {
    "source": "unknown",
    "ac_present": true
  }
}
```

If no PSRAM, `psram_free` field is omitted (not set to 0).

---

## Architecture

### New Component: system_console

```
firmware/components/system_console/
├── CMakeLists.txt
├── Kconfig
├── include/
│   └── system_console.h
└── src/
    └── system_console.c
```

**Dependencies:**
- `esp_console`
- `argtable3`
- `device_identity`
- `freertos` (for timer)

### Shared Reboot Logic

Both console and API use common functions:

```c
// Schedule reboot with countdown
esp_err_t system_reboot_schedule(uint32_t delay_seconds);

// Cancel pending reboot
esp_err_t system_reboot_cancel(void);

// Check if reboot is pending
bool system_reboot_is_pending(void);

// Get seconds until reboot (0 if not pending)
uint32_t system_reboot_get_remaining(void);
```

### Web Auth Enhancement

Add to `web_auth` component:

```c
// Change password (validates current first)
esp_err_t web_auth_change_password(const char *current, const char *new_pass);

// Invalidate all sessions
void web_auth_invalidate_all_sessions(void);
```

---

## Security Considerations

| Action | Console | Web API | Notes |
|--------|---------|---------|-------|
| Reboot | Yes | Yes (auth required) | Non-destructive |
| Factory Reset | No | Yes (auth + confirm) | Destructive, web-only |
| Password Change | No | Yes (auth + current pass) | Web-only |
| View Credentials | No | Limited (first boot only) | Physical access for serial |

**Rationale:**
- Console access implies physical access (acceptable for reboot/status)
- Factory reset too destructive for console (accidental execution risk)
- Password changes via authenticated web UI only

---

## Testing

### Console Commands
- Verify `reboot` countdown displays correctly
- Verify `reboot cancel` stops pending reboot
- Verify `reboot now` executes immediately
- Verify `free` handles missing PSRAM gracefully
- Verify `status` aggregates all subsystem info

### API Endpoints
- Verify auth/status returns correct state
- Verify password change invalidates sessions
- Verify reboot API honors delay parameter
- Verify factory-reset requires confirm flag
- Verify QR endpoint respects first-boot state

---

## Implementation Notes

- Use `esp_timer_get_time()` for uptime (microseconds since boot)
- Use `heap_caps_get_free_size()` with appropriate caps for memory
- Use `esp_chip_info()` for chip details
- Use `esp_get_idf_version()` for IDF version string
- Reboot timer callback runs on timer task - keep minimal, just call `esp_restart()`

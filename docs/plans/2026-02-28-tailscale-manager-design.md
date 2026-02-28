# Tailscale Manager Component Design

**Date:** 2026-02-28
**Status:** Approved
**Depends on:** wifi_manager component (v0.1.0)

## Overview

The `tailscale_manager` component provides Tailscale VPN connectivity for ESP32-S3 devices via the MicroLink library. It wraps MicroLink's polling-based API in an event-driven interface consistent with `wifi_manager`, handles auth key storage in NVS, and provides serial console commands for provisioning.

**Key Capabilities:**
- Tailscale VPN connection via MicroLink
- Auth key storage in encrypted NVS
- Serial console provisioning (`ts_auth`, `ts_clear`, `ts_status`)
- Event callbacks for state changes
- Exponential backoff reconnection
- Thread-safe state management
- Optional/graceful degradation (works locally without Tailscale)

## Architecture

```
firmware/components/tailscale_manager/
├── CMakeLists.txt
├── Kconfig
├── include/
│   └── tailscale_manager.h          # Public API
└── src/
    ├── tailscale_manager.c          # Core: init, connect, state machine
    ├── tailscale_nvs.c              # NVS storage for auth key
    └── tailscale_console.c          # Serial commands

firmware/components/microlink/        # Git submodule (untouched)
```

### Component Dependencies

```cmake
idf_component_register(
    SRCS "src/tailscale_manager.c" "src/tailscale_nvs.c" "src/tailscale_console.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_console wifi_manager microlink
)
```

### State Machine

```
IDLE ──(auth key exists)──► CONNECTING ──(connected)──► CONNECTED
  │                              │                           │
  │                              ▼                           ▼
  └──(no key)──► UNCONFIGURED   RECONNECTING ◄──(disconnect)─┘
```

| State | Description |
|-------|-------------|
| IDLE | Initial state before init |
| UNCONFIGURED | No auth key in NVS, waiting for provisioning |
| CONNECTING | Auth key exists, attempting Tailscale connection |
| CONNECTED | Successfully connected to tailnet |
| RECONNECTING | Lost connection, retrying with exponential backoff |

## Public API

```c
// tailscale_manager.h

#pragma once
#include "esp_err.h"
#include <stdbool.h>

// Event types for callbacks
typedef enum {
    TS_MGR_EVENT_CONNECTED,           // Got Tailscale IP
    TS_MGR_EVENT_DISCONNECTED,        // Lost connection, will retry
    TS_MGR_EVENT_UNCONFIGURED,        // No auth key in NVS
    TS_MGR_EVENT_AUTH_FAILED,         // Invalid/expired auth key
    TS_MGR_EVENT_RECONNECT_EXHAUSTED, // Max reconnection attempts reached
    TS_MGR_EVENT_KEY_UPDATED,         // New auth key set via ts_mgr_set_auth_key()
} ts_mgr_event_t;

// Callback signature
typedef void (*ts_mgr_callback_t)(ts_mgr_event_t event, void *ctx);

// Configuration
typedef struct {
    const char *device_name;       // Tailscale device name (NULL = use Kconfig)
    ts_mgr_callback_t callback;    // Event callback (optional)
    void *callback_ctx;            // Context passed to callback
} ts_mgr_config_t;

// Lifecycle
esp_err_t ts_mgr_init(const ts_mgr_config_t *config);
esp_err_t ts_mgr_stop(void);

// Status
bool ts_mgr_is_connected(void);
bool ts_mgr_is_configured(void);
const char* ts_mgr_get_state_name(void);
esp_err_t ts_mgr_get_ip(char *buf, size_t len);

// Auth key management (for provisioning)
esp_err_t ts_mgr_set_auth_key(const char *key);  // Stores in NVS, triggers connect
esp_err_t ts_mgr_clear_auth_key(void);           // Removes from NVS
bool ts_mgr_has_auth_key(void);                  // Check if key exists
```

## Kconfig Options

```kconfig
menu "Tailscale Manager"

    config TS_MGR_DEVICE_NAME
        string "Default Tailscale device name"
        default "boorker"
        help
            Device name shown in Tailscale admin console.

    config TS_MGR_UPDATE_INTERVAL_MS
        int "MicroLink update interval (ms)"
        default 100
        help
            How often to call microlink_update(). Lower = more responsive,
            higher = less CPU usage.

    config TS_MGR_RECONNECT_INITIAL_MS
        int "Initial reconnection delay (ms)"
        default 5000
        help
            First retry delay after disconnect.

    config TS_MGR_RECONNECT_MAX_MS
        int "Maximum reconnection delay (ms)"
        default 300000
        help
            Exponential backoff caps at this value (default 5 minutes).

    config TS_MGR_RECONNECT_MAX_ATTEMPTS
        int "Maximum reconnection attempts (0 = infinite)"
        default 0
        help
            Number of reconnection attempts before firing
            TS_MGR_EVENT_RECONNECT_EXHAUSTED callback.
            Set to 0 for infinite retries (default behavior).

    config TS_MGR_TASK_STACK_SIZE
        int "Update task stack size"
        default 8192
        help
            Stack size for the MicroLink update task.

    config TS_MGR_TASK_PRIORITY
        int "Update task priority"
        default 5
        help
            FreeRTOS priority for the MicroLink update task.

endmenu
```

## Serial Console Commands

Three commands for auth key provisioning:

| Command | Description |
|---------|-------------|
| `ts_auth <key>` | Set Tailscale auth key (stored in NVS, triggers connect) |
| `ts_clear` | Clear stored auth key (disables Tailscale) |
| `ts_status` | Show connection status and Tailscale IP |

### Example Usage

```
esp32> ts_status
Tailscale Status:
  State: UNCONFIGURED
  Configured: no

esp32> ts_auth tskey-auth-kXYZ123abcdefghijklmnop
Auth key stored. Connecting to Tailscale...

esp32> ts_status
Tailscale Status:
  State: CONNECTED
  Configured: yes
  Tailscale IP: 100.64.0.42
```

## Internal Implementation

### Internal State Structure

```c
static struct {
    ts_mgr_state_t state;
    SemaphoreHandle_t state_mutex;      // Thread safety for state transitions
    microlink_t *ml_handle;
    TaskHandle_t update_task;           // Runs microlink_update() loop
    ts_mgr_callback_t callback;
    void *callback_ctx;
    const char *device_name;
    uint8_t reconnect_attempts;         // Track for exhaustion event
    uint32_t reconnect_delay_ms;        // Exponential backoff
} s_ts_mgr;
```

### Update Task

Dedicated FreeRTOS task runs `microlink_update()` at configured interval:

```c
static void ts_update_task(void *arg) {
    while (1) {
        if (s_ts_mgr.ml_handle && s_ts_mgr.state != TS_MGR_STATE_UNCONFIGURED) {
            microlink_update(s_ts_mgr.ml_handle);

            // Check for state changes with mutex protection
            bool connected = microlink_is_connected(s_ts_mgr.ml_handle);
            // ... handle state transitions, fire callbacks ...
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_TS_MGR_UPDATE_INTERVAL_MS));
    }
}
```

### NVS Storage

Auth keys stored in dedicated NVS namespace:

```c
#define NVS_NAMESPACE "tailscale"
#define NVS_KEY_AUTH  "auth_key"
```

**Security:** Auth key is NEVER logged. Only key length is logged for debugging.

### Reconnection with Exponential Backoff

Mirrors wifi_manager pattern:
- Initial delay: `CONFIG_TS_MGR_RECONNECT_INITIAL_MS` (5s)
- Doubles each attempt up to `CONFIG_TS_MGR_RECONNECT_MAX_MS` (5min)
- Optional max attempts with `TS_MGR_EVENT_RECONNECT_EXHAUSTED`

## Integration

### main.c Integration

```c
#include "wifi_manager.h"
#include "tailscale_manager.h"

static void wifi_callback(wifi_mgr_event_t event, void *ctx) {
    if (event == WIFI_MGR_EVENT_CONNECTED) {
        ts_mgr_config_t ts_config = {
            .device_name = "boorker-dev",
            .callback = tailscale_callback,
        };
        ts_mgr_init(&ts_config);
    }
}

static void tailscale_callback(ts_mgr_event_t event, void *ctx) {
    switch (event) {
        case TS_MGR_EVENT_CONNECTED:
            ESP_LOGI(TAG, "Tailscale connected!");
            break;
        case TS_MGR_EVENT_UNCONFIGURED:
            ESP_LOGI(TAG, "Use 'ts_auth <key>' to configure Tailscale");
            break;
    }
}

void app_main(void) {
    // NVS init...

    // Console init + ts_console_register()

    // WiFi init (Tailscale init happens in callback after WiFi connects)
    wifi_mgr_init(&wifi_config);

    // Main loop...
}
```

### Startup Flow

```
app_main()
    │
    ├── esp_console init + ts_console_register()
    │
    ├── wifi_mgr_init() ──► [waits for connection]
    │                              │
    │                    WIFI_MGR_EVENT_CONNECTED
    │                              │
    │                    ts_mgr_init()
    │                              │
    │               ┌──────────────┴──────────────┐
    │               │                             │
    │         [key exists]                  [no key]
    │               │                             │
    │        microlink_init()           TS_MGR_EVENT_UNCONFIGURED
    │        microlink_connect()               │
    │               │                    [device works locally]
    │        update_task starts                │
    │               │                    [user runs ts_auth]
    │        TS_MGR_EVENT_CONNECTED            │
    │               │                    [triggers connect]
    └───────────────┴─────────────────────────────┘
```

## Error Handling

| Condition | Behavior |
|-----------|----------|
| No auth key in NVS | State: UNCONFIGURED, fires TS_MGR_EVENT_UNCONFIGURED, device works locally |
| Invalid key format | `ts_mgr_set_auth_key()` returns ESP_ERR_INVALID_ARG |
| Expired/revoked key | MicroLink fails auth, fires TS_MGR_EVENT_AUTH_FAILED |
| Network loss | State: RECONNECTING, exponential backoff, fires TS_MGR_EVENT_DISCONNECTED |
| WiFi disconnects | MicroLink naturally fails, handled by reconnect logic |
| Max reconnect reached | Fires TS_MGR_EVENT_RECONNECT_EXHAUSTED, stops retrying |

## Security Considerations

1. **Auth key never logged** - Only key length shown for debugging
2. **NVS encryption** - Keys stored in NVS (encrypted partition recommended)
3. **Key validation** - `ts_mgr_set_auth_key()` validates format before storing
4. **No hardcoded keys** - Must be provisioned at runtime

## Future Enhancements (Out of Scope)

- Web UI provisioning (Phase 4)
- QR code auth key input
- Multiple tailnet support
- Custom DERP server configuration

## Dependencies

| Component | Version | Notes |
|-----------|---------|-------|
| MicroLink | latest | Git submodule |
| ESP-IDF | v5.5.3 | PSRAM required |
| wifi_manager | v0.1.0 | WiFi must be connected first |

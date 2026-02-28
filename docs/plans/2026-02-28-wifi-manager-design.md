# WiFi Manager Component Design

> **For Claude:** Use superpowers:writing-plans to create implementation plan from this design.

**Date:** 2026-02-28
**Status:** Approved
**Component:** `firmware/components/wifi_manager/`

---

## Overview

A reusable ESP-IDF WiFi manager component for the Boorker project. Handles WiFi connection, BLE provisioning, credential storage, mDNS discovery, and power management. Designed to work across ESP32-S3 boards (DevKitC, Heltec V3).

---

## Requirements

| Requirement | Decision |
|-------------|----------|
| Provisioning | BLE via `wifi_provisioning` component |
| Credential storage | NVS with encryption |
| Reconnection | Infinite retry, exponential backoff (1s → 5min) |
| Discovery | mDNS (`<device>.local`) |
| Power management | Light sleep + modem sleep |
| Security | WPA3 preferred (WPA2 fallback), NVS encryption, BLE Proof-of-Possession |
| Events | Callbacks + FreeRTOS event groups |
| Reusability | ESP-IDF component in monorepo |

---

## Architecture

### Component Structure

```
firmware/components/wifi_manager/
├── CMakeLists.txt           # Component build config
├── Kconfig                  # Menuconfig options
├── include/
│   └── wifi_manager.h       # Public API
└── src/
    ├── wifi_manager.c       # Core state machine + events
    ├── wifi_provisioning.c  # BLE provisioning wrapper
    ├── wifi_power.c         # Sleep mode management
    └── wifi_mdns.c          # mDNS registration
```

### State Machine

```
┌─────────────┐
│    IDLE     │  ← Just initialized
└──────┬──────┘
       │ init()
       ▼
┌─────────────┐     no stored creds
│ CONNECTING  │ ──────────────────────┐
└──────┬──────┘                       │
       │ success                      ▼
       ▼                       ┌─────────────┐
┌─────────────┐                │PROVISIONING │ ← BLE active
│  CONNECTED  │                └──────┬──────┘
└──────┬──────┘                       │ creds received
       │ disconnect                   │
       ▼                              │
┌─────────────┐                       │
│RECONNECTING │ ◄─────────────────────┘
└──────┬──────┘
       │ exponential backoff
       ▼
┌─────────────┐
│  CONNECTED  │
└─────────────┘
```

**Behavior:**
1. On boot → check NVS for stored credentials
2. If credentials exist → attempt connection
3. If no credentials (or forced) → start BLE provisioning
4. On disconnect → exponential backoff reconnection (never gives up)
5. Backoff sequence: 1s, 2s, 4s, 8s, 16s, 32s, 64s, 128s, 256s, 300s (cap)

---

## Public API

```c
#pragma once
#include "esp_err.h"
#include "freertos/event_groups.h"
#include <stdbool.h>

// Event types for callbacks
typedef enum {
    WIFI_MGR_EVENT_CONNECTED,      // Got IP address
    WIFI_MGR_EVENT_DISCONNECTED,   // Lost connection
    WIFI_MGR_EVENT_PROVISIONING,   // Entered provisioning mode
    WIFI_MGR_EVENT_PROVISIONED,    // Credentials received via BLE
} wifi_mgr_event_t;

// Callback signature
typedef void (*wifi_mgr_callback_t)(wifi_mgr_event_t event, void *ctx);

// Configuration
typedef struct {
    const char *device_name;       // For mDNS and BLE provisioning
    bool start_provisioning;       // Force provisioning even if creds exist
    wifi_mgr_callback_t callback;  // Event callback (optional)
    void *callback_ctx;            // Context passed to callback
} wifi_mgr_config_t;

// Initialize and start WiFi manager
// Blocks until connected or provisioning mode started
esp_err_t wifi_mgr_init(const wifi_mgr_config_t *config);

// State queries
bool wifi_mgr_is_connected(void);
esp_err_t wifi_mgr_get_ip(char *buf, size_t len);
const char* wifi_mgr_get_state_name(void);  // For logging

// Event group for task synchronization
#define WIFI_MGR_CONNECTED_BIT    BIT0
#define WIFI_MGR_DISCONNECTED_BIT BIT1
#define WIFI_MGR_PROVISIONED_BIT  BIT2
EventGroupHandle_t wifi_mgr_get_event_group(void);

// Manual control
esp_err_t wifi_mgr_start_provisioning(void);  // Force re-provisioning
esp_err_t wifi_mgr_clear_credentials(void);   // Erase stored credentials
esp_err_t wifi_mgr_stop(void);                // Disconnect and cleanup
```

---

## Kconfig Options

```kconfig
menu "WiFi Manager"

    config WIFI_MGR_DEVICE_NAME
        string "Device name for mDNS and provisioning"
        default "boorker"
        help
            Used for mDNS (name.local) and BLE advertising name.
            Should be unique on local network.

    config WIFI_MGR_PROV_POP
        string "Proof-of-Possession PIN for BLE provisioning"
        default "boorker123"
        help
            User must enter this PIN in the ESP BLE Prov app.
            CHANGE THIS for production devices!

    config WIFI_MGR_RECONNECT_INITIAL_DELAY_MS
        int "Initial reconnection delay (ms)"
        default 1000
        help
            First retry delay after disconnect

    config WIFI_MGR_RECONNECT_MAX_DELAY_MS
        int "Maximum reconnection delay (ms)"
        default 300000
        help
            Exponential backoff caps at this value (default 5 minutes)

    config WIFI_MGR_ENABLE_POWER_SAVE
        bool "Enable WiFi power saving (modem sleep)"
        default y
        help
            Reduces power consumption when idle.
            Adds slight latency to first packet after idle.

    config WIFI_MGR_ENABLE_NVS_ENCRYPTION
        bool "Encrypt stored credentials"
        default y
        select NVS_ENCRYPTION
        help
            Encrypts WiFi credentials in flash storage.
            Requires NVS encryption key in partition table.

    config WIFI_MGR_ENABLE_MDNS
        bool "Enable mDNS discovery"
        default y
        help
            Register device as <name>.local for easy discovery.

endmenu
```

---

## Security

### Development Security (Implemented Now)

| Protection | Implementation |
|------------|----------------|
| Credential storage | NVS encryption enabled |
| BLE provisioning | Proof-of-Possession PIN required |
| WiFi authentication | WPA3 preferred, WPA2 fallback |
| Network traffic | TLS via Tailscale (handled elsewhere) |

### Production Security (Future)

| Protection | Implementation |
|------------|----------------|
| Firmware integrity | Secure boot v2 (signed images) |
| Flash protection | Flash encryption |
| Debug lockdown | JTAG disabled via eFuse |

---

## Dependencies

```cmake
# Required ESP-IDF components
REQUIRES
    nvs_flash
    esp_wifi
    esp_event
    esp_netif
    wifi_provisioning
    protocomm
    bt
    mdns
```

---

## Usage Example

```c
#include "wifi_manager.h"

static void wifi_event_handler(wifi_mgr_event_t event, void *ctx) {
    switch (event) {
        case WIFI_MGR_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected - starting services");
            start_tailscale();
            start_webserver();
            break;
        case WIFI_MGR_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi disconnected - services paused");
            break;
        case WIFI_MGR_EVENT_PROVISIONING:
            ESP_LOGI(TAG, "Open ESP BLE Prov app to configure WiFi");
            break;
        default:
            break;
    }
}

void app_main(void) {
    nvs_flash_init();  // Required before wifi_mgr_init

    wifi_mgr_config_t config = {
        .device_name = "boorker-dev",
        .start_provisioning = false,  // Only if no stored creds
        .callback = wifi_event_handler,
        .callback_ctx = NULL,
    };

    esp_err_t ret = wifi_mgr_init(&config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi manager init failed");
        return;
    }

    // For tasks that need to wait for WiFi:
    EventGroupHandle_t events = wifi_mgr_get_event_group();
    xEventGroupWaitBits(events, WIFI_MGR_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    // WiFi is now connected
    char ip[16];
    wifi_mgr_get_ip(ip, sizeof(ip));
    ESP_LOGI(TAG, "Connected with IP: %s", ip);
}
```

---

## File Estimates

| File | Lines | Purpose |
|------|-------|---------|
| `wifi_manager.h` | ~60 | Public API |
| `wifi_manager.c` | ~300 | State machine, events, reconnection |
| `wifi_provisioning.c` | ~150 | BLE provisioning setup |
| `wifi_power.c` | ~80 | Power save configuration |
| `wifi_mdns.c` | ~60 | mDNS service registration |
| `Kconfig` | ~50 | Menuconfig options |
| `CMakeLists.txt` | ~15 | Build configuration |
| **Total** | ~715 | |

---

## Open Questions (Resolved)

| Question | Resolution |
|----------|------------|
| Separate repo vs monorepo? | Monorepo - simpler, can extract later |
| Production security now? | No - dev security only, production later |
| Which provisioning transport? | BLE (cleaner, works without network) |

---

## Next Steps

1. Create implementation plan via `superpowers:writing-plans`
2. Implement component files
3. Test with ESP BLE Prov mobile app
4. Integrate into main.c
5. Update original setup plan to reference this component

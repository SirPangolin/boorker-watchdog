# WiFi Manager Component Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement a reusable ESP-IDF WiFi manager component with BLE provisioning, exponential backoff reconnection, mDNS discovery, and power management.

**Architecture:** ESP-IDF component in `firmware/components/wifi_manager/` using FreeRTOS event groups for state synchronization, ESP-IDF wifi_provisioning for BLE setup, and NVS for credential storage. State machine handles IDLE → CONNECTING → CONNECTED / PROVISIONING → RECONNECTING flow.

**Tech Stack:** ESP-IDF v5.5.3, wifi_provisioning, protocomm, bt, mdns, nvs_flash, FreeRTOS

**Design Document:** [WiFi Manager Design](./2026-02-28-wifi-manager-design.md)

---

## Testing Strategy

ESP-IDF embedded development uses device-based verification rather than host unit tests:

| Verification Type | Method |
|-------------------|--------|
| Compilation | `idf.py build` - catches syntax, type errors, missing includes |
| Linking | Build success - verifies all symbols resolve |
| Runtime | `idf.py flash && /esp:monitor` - observe serial output |
| Functional | ESP BLE Prov app + manual testing |

Each task includes verification steps appropriate to embedded development.

---

## Task 1: Create Component Directory Structure

**Files:**
- Create: `firmware/components/wifi_manager/CMakeLists.txt`
- Create: `firmware/components/wifi_manager/Kconfig`
- Create: `firmware/components/wifi_manager/include/wifi_manager.h`
- Create: `firmware/components/wifi_manager/src/wifi_manager.c` (stub)
- Delete: `firmware/components/.gitkeep`

**Step 1: Create CMakeLists.txt**

```cmake
idf_component_register(
    SRCS
        "src/wifi_manager.c"
        "src/wifi_provisioning.c"
        "src/wifi_power.c"
        "src/wifi_mdns.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        wifi_provisioning
        protocomm
        bt
        mdns
        esp_timer
        log
)
```

**Step 2: Create Kconfig**

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

    config WIFI_MGR_ENABLE_MDNS
        bool "Enable mDNS discovery"
        default y
        help
            Register device as <name>.local for easy discovery.

endmenu
```

**Step 3: Create wifi_manager.h (public API)**

```c
#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Event bits for synchronization
#define WIFI_MGR_CONNECTED_BIT    BIT0
#define WIFI_MGR_DISCONNECTED_BIT BIT1
#define WIFI_MGR_PROVISIONED_BIT  BIT2

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
    const char *device_name;       // For mDNS and BLE provisioning (NULL = use Kconfig)
    bool start_provisioning;       // Force provisioning even if creds exist
    wifi_mgr_callback_t callback;  // Event callback (optional)
    void *callback_ctx;            // Context passed to callback
} wifi_mgr_config_t;

/**
 * @brief Initialize and start WiFi manager
 *
 * This function initializes WiFi, checks for stored credentials,
 * and either connects to the saved network or starts BLE provisioning.
 *
 * @param config Configuration struct (can be NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t wifi_mgr_init(const wifi_mgr_config_t *config);

/**
 * @brief Check if WiFi is connected
 * @return true if connected with IP address
 */
bool wifi_mgr_is_connected(void);

/**
 * @brief Get current IP address
 * @param buf Buffer to write IP string
 * @param len Buffer length (minimum 16 bytes for IPv4)
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not connected
 */
esp_err_t wifi_mgr_get_ip(char *buf, size_t len);

/**
 * @brief Get current state name for logging
 * @return State name string (e.g., "CONNECTED", "PROVISIONING")
 */
const char* wifi_mgr_get_state_name(void);

/**
 * @brief Get event group for task synchronization
 * @return FreeRTOS event group handle
 */
EventGroupHandle_t wifi_mgr_get_event_group(void);

/**
 * @brief Force start BLE provisioning
 *
 * Disconnects from current network (if any) and starts BLE provisioning.
 * Useful for re-provisioning to a different network.
 *
 * @return ESP_OK on success
 */
esp_err_t wifi_mgr_start_provisioning(void);

/**
 * @brief Clear stored WiFi credentials
 * @return ESP_OK on success
 */
esp_err_t wifi_mgr_clear_credentials(void);

/**
 * @brief Stop WiFi manager and cleanup
 * @return ESP_OK on success
 */
esp_err_t wifi_mgr_stop(void);

#ifdef __cplusplus
}
#endif
```

**Step 4: Create wifi_manager.c (stub)**

```c
#include "wifi_manager.h"
#include "esp_log.h"

static const char *TAG = "wifi_manager";

esp_err_t wifi_mgr_init(const wifi_mgr_config_t *config)
{
    ESP_LOGI(TAG, "wifi_mgr_init stub called");
    return ESP_OK;
}

bool wifi_mgr_is_connected(void)
{
    return false;
}

esp_err_t wifi_mgr_get_ip(char *buf, size_t len)
{
    return ESP_ERR_INVALID_STATE;
}

const char* wifi_mgr_get_state_name(void)
{
    return "STUB";
}

EventGroupHandle_t wifi_mgr_get_event_group(void)
{
    return NULL;
}

esp_err_t wifi_mgr_start_provisioning(void)
{
    return ESP_OK;
}

esp_err_t wifi_mgr_clear_credentials(void)
{
    return ESP_OK;
}

esp_err_t wifi_mgr_stop(void)
{
    return ESP_OK;
}
```

**Step 5: Create other source stubs**

Create `firmware/components/wifi_manager/src/wifi_provisioning.c`:
```c
#include "esp_log.h"

static const char *TAG = "wifi_prov";

void wifi_prov_stub(void) {
    ESP_LOGI(TAG, "provisioning stub");
}
```

Create `firmware/components/wifi_manager/src/wifi_power.c`:
```c
#include "esp_log.h"

static const char *TAG = "wifi_power";

void wifi_power_stub(void) {
    ESP_LOGI(TAG, "power stub");
}
```

Create `firmware/components/wifi_manager/src/wifi_mdns.c`:
```c
#include "esp_log.h"

static const char *TAG = "wifi_mdns";

void wifi_mdns_stub(void) {
    ESP_LOGI(TAG, "mdns stub");
}
```

**Step 6: Delete .gitkeep**

```bash
rm firmware/components/.gitkeep
```

**Step 7: Verify build**

```bash
source ~/esp/esp-idf/export.sh
cd ~/claude/boorker-watchdog/firmware
idf.py build
```

Expected: Build succeeds with component registered

**Step 8: Commit**

```bash
git add firmware/components/wifi_manager/
git rm firmware/components/.gitkeep
git commit -m "feat(wifi_manager): scaffold component structure

- Add CMakeLists.txt with ESP-IDF dependencies
- Add Kconfig for menuconfig options
- Add public API header with types and function declarations
- Add source file stubs for incremental implementation"
```

---

## Task 2: Implement Core State Machine

**Files:**
- Modify: `firmware/components/wifi_manager/src/wifi_manager.c`

**Step 1: Add includes and internal types**

Replace the stub content in `wifi_manager.c`:

```c
#include "wifi_manager.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "nvs_flash.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <string.h>

static const char *TAG = "wifi_manager";

// Internal state machine
typedef enum {
    WIFI_MGR_STATE_IDLE,
    WIFI_MGR_STATE_CONNECTING,
    WIFI_MGR_STATE_CONNECTED,
    WIFI_MGR_STATE_PROVISIONING,
    WIFI_MGR_STATE_RECONNECTING,
} wifi_mgr_state_t;

// Internal state
static struct {
    wifi_mgr_state_t state;
    EventGroupHandle_t event_group;
    wifi_mgr_callback_t callback;
    void *callback_ctx;
    const char *device_name;
    esp_netif_t *sta_netif;
    esp_timer_handle_t reconnect_timer;
    uint32_t reconnect_delay_ms;
    bool wifi_started;
} s_wifi_mgr = {
    .state = WIFI_MGR_STATE_IDLE,
    .reconnect_delay_ms = CONFIG_WIFI_MGR_RECONNECT_INITIAL_DELAY_MS,
};

// Forward declarations
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data);
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data);
static void reconnect_timer_callback(void *arg);
static void set_state(wifi_mgr_state_t new_state);
static void notify_callback(wifi_mgr_event_t event);
static bool has_stored_credentials(void);

// State name lookup
static const char *state_names[] = {
    [WIFI_MGR_STATE_IDLE] = "IDLE",
    [WIFI_MGR_STATE_CONNECTING] = "CONNECTING",
    [WIFI_MGR_STATE_CONNECTED] = "CONNECTED",
    [WIFI_MGR_STATE_PROVISIONING] = "PROVISIONING",
    [WIFI_MGR_STATE_RECONNECTING] = "RECONNECTING",
};
```

**Step 2: Implement initialization**

```c
esp_err_t wifi_mgr_init(const wifi_mgr_config_t *config)
{
    ESP_LOGI(TAG, "Initializing WiFi manager...");

    // Create event group
    s_wifi_mgr.event_group = xEventGroupCreate();
    if (s_wifi_mgr.event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_ERR_NO_MEM;
    }

    // Store config
    if (config) {
        s_wifi_mgr.callback = config->callback;
        s_wifi_mgr.callback_ctx = config->callback_ctx;
        s_wifi_mgr.device_name = config->device_name ? config->device_name
                                                     : CONFIG_WIFI_MGR_DEVICE_NAME;
    } else {
        s_wifi_mgr.device_name = CONFIG_WIFI_MGR_DEVICE_NAME;
    }

    // Initialize TCP/IP stack
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // Create default WiFi station
    s_wifi_mgr.sta_netif = esp_netif_create_default_wifi_sta();

    // Initialize WiFi
    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

    // Register event handlers
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL, NULL));

    // Set WiFi mode and start
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_mgr.wifi_started = true;

    // Create reconnect timer
    esp_timer_create_args_t timer_args = {
        .callback = reconnect_timer_callback,
        .name = "wifi_reconnect",
    };
    ESP_ERROR_CHECK(esp_timer_create(&timer_args, &s_wifi_mgr.reconnect_timer));

    // Check for stored credentials
    if (config && config->start_provisioning) {
        ESP_LOGI(TAG, "Force provisioning requested");
        return wifi_mgr_start_provisioning();
    }

    if (has_stored_credentials()) {
        ESP_LOGI(TAG, "Found stored credentials, connecting...");
        set_state(WIFI_MGR_STATE_CONNECTING);
        esp_wifi_connect();
    } else {
        ESP_LOGI(TAG, "No stored credentials, starting provisioning...");
        return wifi_mgr_start_provisioning();
    }

    return ESP_OK;
}
```

**Step 3: Implement event handlers**

```c
static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    switch (event_id) {
        case WIFI_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi STA started");
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "Disconnected from AP (reason: %d)", event->reason);

            xEventGroupClearBits(s_wifi_mgr.event_group, WIFI_MGR_CONNECTED_BIT);
            xEventGroupSetBits(s_wifi_mgr.event_group, WIFI_MGR_DISCONNECTED_BIT);

            if (s_wifi_mgr.state == WIFI_MGR_STATE_CONNECTED ||
                s_wifi_mgr.state == WIFI_MGR_STATE_CONNECTING) {
                set_state(WIFI_MGR_STATE_RECONNECTING);
                notify_callback(WIFI_MGR_EVENT_DISCONNECTED);

                // Start exponential backoff reconnection
                ESP_LOGI(TAG, "Reconnecting in %lu ms...", s_wifi_mgr.reconnect_delay_ms);
                esp_timer_start_once(s_wifi_mgr.reconnect_timer,
                                     s_wifi_mgr.reconnect_delay_ms * 1000);
            }
            break;
        }

        case WIFI_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "Connected to AP, waiting for IP...");
            break;

        default:
            break;
    }
}

static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        // Reset reconnect delay on successful connection
        s_wifi_mgr.reconnect_delay_ms = CONFIG_WIFI_MGR_RECONNECT_INITIAL_DELAY_MS;

        set_state(WIFI_MGR_STATE_CONNECTED);

        xEventGroupClearBits(s_wifi_mgr.event_group, WIFI_MGR_DISCONNECTED_BIT);
        xEventGroupSetBits(s_wifi_mgr.event_group, WIFI_MGR_CONNECTED_BIT);

        notify_callback(WIFI_MGR_EVENT_CONNECTED);
    }
}
```

**Step 4: Implement helper functions**

```c
static void reconnect_timer_callback(void *arg)
{
    ESP_LOGI(TAG, "Attempting reconnection...");
    set_state(WIFI_MGR_STATE_CONNECTING);
    esp_wifi_connect();

    // Increase delay for next attempt (exponential backoff)
    s_wifi_mgr.reconnect_delay_ms *= 2;
    if (s_wifi_mgr.reconnect_delay_ms > CONFIG_WIFI_MGR_RECONNECT_MAX_DELAY_MS) {
        s_wifi_mgr.reconnect_delay_ms = CONFIG_WIFI_MGR_RECONNECT_MAX_DELAY_MS;
    }
}

static void set_state(wifi_mgr_state_t new_state)
{
    if (s_wifi_mgr.state != new_state) {
        ESP_LOGI(TAG, "State: %s -> %s",
                 state_names[s_wifi_mgr.state],
                 state_names[new_state]);
        s_wifi_mgr.state = new_state;
    }
}

static void notify_callback(wifi_mgr_event_t event)
{
    if (s_wifi_mgr.callback) {
        s_wifi_mgr.callback(event, s_wifi_mgr.callback_ctx);
    }
}

static bool has_stored_credentials(void)
{
    wifi_config_t wifi_config;
    esp_err_t ret = esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) {
        return false;
    }
    return wifi_config.sta.ssid[0] != '\0';
}
```

**Step 5: Implement public API functions**

```c
bool wifi_mgr_is_connected(void)
{
    return s_wifi_mgr.state == WIFI_MGR_STATE_CONNECTED;
}

esp_err_t wifi_mgr_get_ip(char *buf, size_t len)
{
    if (!wifi_mgr_is_connected() || !s_wifi_mgr.sta_netif) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_netif_ip_info_t ip_info;
    esp_err_t ret = esp_netif_get_ip_info(s_wifi_mgr.sta_netif, &ip_info);
    if (ret != ESP_OK) {
        return ret;
    }

    snprintf(buf, len, IPSTR, IP2STR(&ip_info.ip));
    return ESP_OK;
}

const char* wifi_mgr_get_state_name(void)
{
    return state_names[s_wifi_mgr.state];
}

EventGroupHandle_t wifi_mgr_get_event_group(void)
{
    return s_wifi_mgr.event_group;
}

esp_err_t wifi_mgr_start_provisioning(void)
{
    ESP_LOGI(TAG, "Starting provisioning... (stub)");
    set_state(WIFI_MGR_STATE_PROVISIONING);
    notify_callback(WIFI_MGR_EVENT_PROVISIONING);
    return ESP_OK;
}

esp_err_t wifi_mgr_clear_credentials(void)
{
    wifi_config_t wifi_config = {0};
    return esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
}

esp_err_t wifi_mgr_stop(void)
{
    if (s_wifi_mgr.reconnect_timer) {
        esp_timer_stop(s_wifi_mgr.reconnect_timer);
        esp_timer_delete(s_wifi_mgr.reconnect_timer);
        s_wifi_mgr.reconnect_timer = NULL;
    }

    if (s_wifi_mgr.wifi_started) {
        esp_wifi_stop();
        s_wifi_mgr.wifi_started = false;
    }

    if (s_wifi_mgr.event_group) {
        vEventGroupDelete(s_wifi_mgr.event_group);
        s_wifi_mgr.event_group = NULL;
    }

    set_state(WIFI_MGR_STATE_IDLE);
    return ESP_OK;
}
```

**Step 6: Verify build**

```bash
cd ~/claude/boorker-watchdog/firmware
idf.py build
```

Expected: Build succeeds

**Step 7: Commit**

```bash
git add firmware/components/wifi_manager/src/wifi_manager.c
git commit -m "feat(wifi_manager): implement core state machine

- Add WiFi initialization with ESP-IDF netif/event system
- Implement state machine: IDLE -> CONNECTING -> CONNECTED
- Add exponential backoff reconnection (never gives up)
- Implement event handlers for WIFI_EVENT and IP_EVENT
- Add public API implementations"
```

---

## Task 3: Implement BLE Provisioning

**Files:**
- Modify: `firmware/components/wifi_manager/src/wifi_provisioning.c`
- Modify: `firmware/components/wifi_manager/src/wifi_manager.c`

**Step 1: Create internal provisioning header**

Create `firmware/components/wifi_manager/src/wifi_prov_internal.h`:

```c
#pragma once

#include "esp_err.h"

/**
 * @brief Start BLE provisioning
 * @param device_name Device name for BLE advertising
 * @return ESP_OK on success
 */
esp_err_t wifi_prov_start(const char *device_name);

/**
 * @brief Stop BLE provisioning
 */
void wifi_prov_stop(void);

/**
 * @brief Check if provisioning is active
 */
bool wifi_prov_is_active(void);
```

**Step 2: Implement wifi_provisioning.c**

Replace the stub in `wifi_provisioning.c`:

```c
#include "wifi_prov_internal.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

static const char *TAG = "wifi_prov";

static bool s_prov_active = false;

// Provisioning event handler
static void prov_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    switch (event_id) {
        case WIFI_PROV_START:
            ESP_LOGI(TAG, "Provisioning started");
            break;

        case WIFI_PROV_CRED_RECV: {
            wifi_sta_config_t *sta = (wifi_sta_config_t *)event_data;
            ESP_LOGI(TAG, "Received credentials for SSID: %s", sta->ssid);
            break;
        }

        case WIFI_PROV_CRED_FAIL: {
            wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
            ESP_LOGE(TAG, "Provisioning failed: %s",
                     (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                     "Auth failed" : "AP not found");
            break;
        }

        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning successful");
            break;

        case WIFI_PROV_END:
            ESP_LOGI(TAG, "Provisioning ended");
            wifi_prov_mgr_deinit();
            s_prov_active = false;
            break;

        default:
            break;
    }
}

esp_err_t wifi_prov_start(const char *device_name)
{
    ESP_LOGI(TAG, "Starting BLE provisioning as '%s'...", device_name);

    // Initialize provisioning manager
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
    };

    esp_err_t ret = wifi_prov_mgr_init(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init provisioning manager: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register provisioning event handler
    ESP_ERROR_CHECK(esp_event_handler_register(
        WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler, NULL));

    // Check if already provisioned
    bool provisioned = false;
    wifi_prov_mgr_is_provisioned(&provisioned);
    if (provisioned) {
        ESP_LOGI(TAG, "Already provisioned, clearing for re-provisioning");
        // Continue anyway to allow re-provisioning
    }

    // Configure security (with Proof-of-Possession)
    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
    const char *pop = CONFIG_WIFI_MGR_PROV_POP;

    // Configure service name (BLE device name)
    wifi_prov_scheme_ble_set_service_uuid((uint8_t *)"\x12\x34\x56\x78\x12\x34\x56\x78\x12\x34\x56\x78\x12\x34\x56\x78");

    // Start provisioning
    ret = wifi_prov_mgr_start_provisioning(security, pop, device_name, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning: %s", esp_err_to_name(ret));
        wifi_prov_mgr_deinit();
        return ret;
    }

    s_prov_active = true;
    ESP_LOGI(TAG, "BLE provisioning active. Use ESP BLE Prov app to configure.");
    ESP_LOGI(TAG, "Proof-of-Possession PIN: %s", pop);

    return ESP_OK;
}

void wifi_prov_stop(void)
{
    if (s_prov_active) {
        wifi_prov_mgr_stop_provisioning();
        wifi_prov_mgr_deinit();
        s_prov_active = false;
    }
}

bool wifi_prov_is_active(void)
{
    return s_prov_active;
}
```

**Step 3: Update wifi_manager.c to use provisioning**

Add include at top of `wifi_manager.c`:
```c
#include "wifi_prov_internal.h"
```

Replace the `wifi_mgr_start_provisioning` stub:
```c
esp_err_t wifi_mgr_start_provisioning(void)
{
    // Stop any reconnection attempts
    if (s_wifi_mgr.reconnect_timer) {
        esp_timer_stop(s_wifi_mgr.reconnect_timer);
    }

    // Disconnect if connected
    if (s_wifi_mgr.state == WIFI_MGR_STATE_CONNECTED) {
        esp_wifi_disconnect();
    }

    set_state(WIFI_MGR_STATE_PROVISIONING);
    notify_callback(WIFI_MGR_EVENT_PROVISIONING);

    return wifi_prov_start(s_wifi_mgr.device_name);
}
```

Add provisioning event handler in `wifi_manager.c`:
```c
// Add to includes
#include "wifi_provisioning/manager.h"

// Add new event handler registration in wifi_mgr_init(), after other handlers:
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler_internal, NULL, NULL));

// Add the handler function before wifi_mgr_init():
static void prov_event_handler_internal(void *arg, esp_event_base_t event_base,
                                        int32_t event_id, void *event_data)
{
    switch (event_id) {
        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "Credentials received successfully");
            xEventGroupSetBits(s_wifi_mgr.event_group, WIFI_MGR_PROVISIONED_BIT);
            notify_callback(WIFI_MGR_EVENT_PROVISIONED);
            break;

        case WIFI_PROV_END:
            // Provisioning ended, WiFi should auto-connect
            if (s_wifi_mgr.state == WIFI_MGR_STATE_PROVISIONING) {
                set_state(WIFI_MGR_STATE_CONNECTING);
            }
            break;

        default:
            break;
    }
}
```

**Step 4: Update sdkconfig.defaults for BLE**

Append to `firmware/sdkconfig.defaults`:
```
# Bluetooth for BLE provisioning
CONFIG_BT_ENABLED=y
CONFIG_BT_BLUEDROID_ENABLED=y
CONFIG_BT_BLE_42_FEATURES_SUPPORTED=y
```

**Step 5: Verify build**

```bash
cd ~/claude/boorker-watchdog/firmware
rm -rf build sdkconfig  # Clean rebuild for new Kconfig
idf.py set-target esp32s3
idf.py build
```

Expected: Build succeeds with BT components included

**Step 6: Commit**

```bash
git add firmware/components/wifi_manager/src/
git add firmware/sdkconfig.defaults
git commit -m "feat(wifi_manager): implement BLE provisioning

- Add wifi_prov_internal.h for internal provisioning API
- Implement wifi_provisioning.c with ESP BLE Prov app support
- Configure WIFI_PROV_SECURITY_1 with Proof-of-Possession
- Handle provisioning events (cred received, success, fail)
- Enable Bluetooth in sdkconfig.defaults"
```

---

## Task 4: Implement mDNS Discovery

**Files:**
- Modify: `firmware/components/wifi_manager/src/wifi_mdns.c`
- Modify: `firmware/components/wifi_manager/src/wifi_manager.c`

**Step 1: Create internal mDNS header**

Create `firmware/components/wifi_manager/src/wifi_mdns_internal.h`:

```c
#pragma once

#include "esp_err.h"

/**
 * @brief Start mDNS service
 * @param hostname Hostname for .local resolution
 * @return ESP_OK on success
 */
esp_err_t wifi_mdns_start(const char *hostname);

/**
 * @brief Stop mDNS service
 */
void wifi_mdns_stop(void);
```

**Step 2: Implement wifi_mdns.c**

Replace the stub in `wifi_mdns.c`:

```c
#include "wifi_mdns_internal.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include "mdns.h"

static const char *TAG = "wifi_mdns";

static bool s_mdns_started = false;

esp_err_t wifi_mdns_start(const char *hostname)
{
#if CONFIG_WIFI_MGR_ENABLE_MDNS
    if (s_mdns_started) {
        ESP_LOGW(TAG, "mDNS already started");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting mDNS as '%s.local'...", hostname);

    esp_err_t ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init mDNS: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = mdns_hostname_set(hostname);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set hostname: %s", esp_err_to_name(ret));
        mdns_free();
        return ret;
    }

    ret = mdns_instance_name_set("Boorker IoT Device");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set instance name: %s", esp_err_to_name(ret));
        // Not critical, continue
    }

    // Add HTTP service for discovery
    ret = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add HTTP service: %s", esp_err_to_name(ret));
        // Not critical, continue
    }

    s_mdns_started = true;
    ESP_LOGI(TAG, "mDNS started: %s.local", hostname);

    return ESP_OK;
#else
    ESP_LOGI(TAG, "mDNS disabled in config");
    return ESP_OK;
#endif
}

void wifi_mdns_stop(void)
{
#if CONFIG_WIFI_MGR_ENABLE_MDNS
    if (s_mdns_started) {
        mdns_free();
        s_mdns_started = false;
        ESP_LOGI(TAG, "mDNS stopped");
    }
#endif
}
```

**Step 3: Integrate mDNS into wifi_manager.c**

Add include:
```c
#include "wifi_mdns_internal.h"
```

Update `ip_event_handler` to start mDNS on connection:
```c
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        // Reset reconnect delay on successful connection
        s_wifi_mgr.reconnect_delay_ms = CONFIG_WIFI_MGR_RECONNECT_INITIAL_DELAY_MS;

        set_state(WIFI_MGR_STATE_CONNECTED);

        xEventGroupClearBits(s_wifi_mgr.event_group, WIFI_MGR_DISCONNECTED_BIT);
        xEventGroupSetBits(s_wifi_mgr.event_group, WIFI_MGR_CONNECTED_BIT);

        // Start mDNS
        wifi_mdns_start(s_wifi_mgr.device_name);

        notify_callback(WIFI_MGR_EVENT_CONNECTED);
    }
}
```

Update `wifi_mgr_stop` to stop mDNS:
```c
esp_err_t wifi_mgr_stop(void)
{
    // Stop mDNS
    wifi_mdns_stop();

    if (s_wifi_mgr.reconnect_timer) {
        esp_timer_stop(s_wifi_mgr.reconnect_timer);
        esp_timer_delete(s_wifi_mgr.reconnect_timer);
        s_wifi_mgr.reconnect_timer = NULL;
    }

    if (s_wifi_mgr.wifi_started) {
        esp_wifi_stop();
        s_wifi_mgr.wifi_started = false;
    }

    if (s_wifi_mgr.event_group) {
        vEventGroupDelete(s_wifi_mgr.event_group);
        s_wifi_mgr.event_group = NULL;
    }

    set_state(WIFI_MGR_STATE_IDLE);
    return ESP_OK;
}
```

**Step 4: Verify build**

```bash
cd ~/claude/boorker-watchdog/firmware
idf.py build
```

Expected: Build succeeds

**Step 5: Commit**

```bash
git add firmware/components/wifi_manager/src/
git commit -m "feat(wifi_manager): implement mDNS discovery

- Add wifi_mdns_internal.h for internal mDNS API
- Implement wifi_mdns.c with hostname.local registration
- Start mDNS automatically on WiFi connection
- Register HTTP service for network discovery
- Respect CONFIG_WIFI_MGR_ENABLE_MDNS setting"
```

---

## Task 5: Implement Power Management

**Files:**
- Modify: `firmware/components/wifi_manager/src/wifi_power.c`
- Modify: `firmware/components/wifi_manager/src/wifi_manager.c`

**Step 1: Create internal power header**

Create `firmware/components/wifi_manager/src/wifi_power_internal.h`:

```c
#pragma once

#include "esp_err.h"

/**
 * @brief Enable WiFi power saving mode
 * @return ESP_OK on success
 */
esp_err_t wifi_power_enable(void);

/**
 * @brief Disable WiFi power saving mode
 * @return ESP_OK on success
 */
esp_err_t wifi_power_disable(void);
```

**Step 2: Implement wifi_power.c**

Replace the stub in `wifi_power.c`:

```c
#include "wifi_power_internal.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG = "wifi_power";

esp_err_t wifi_power_enable(void)
{
#if CONFIG_WIFI_MGR_ENABLE_POWER_SAVE
    ESP_LOGI(TAG, "Enabling WiFi power save (modem sleep)...");

    esp_err_t ret = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable power save: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "WiFi power save enabled");
    return ESP_OK;
#else
    ESP_LOGI(TAG, "WiFi power save disabled in config");
    return ESP_OK;
#endif
}

esp_err_t wifi_power_disable(void)
{
    ESP_LOGI(TAG, "Disabling WiFi power save...");

    esp_err_t ret = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable power save: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "WiFi power save disabled");
    return ESP_OK;
}
```

**Step 3: Integrate power management into wifi_manager.c**

Add include:
```c
#include "wifi_power_internal.h"
```

Update `ip_event_handler` to enable power save after connection:
```c
static void ip_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        // Reset reconnect delay on successful connection
        s_wifi_mgr.reconnect_delay_ms = CONFIG_WIFI_MGR_RECONNECT_INITIAL_DELAY_MS;

        set_state(WIFI_MGR_STATE_CONNECTED);

        xEventGroupClearBits(s_wifi_mgr.event_group, WIFI_MGR_DISCONNECTED_BIT);
        xEventGroupSetBits(s_wifi_mgr.event_group, WIFI_MGR_CONNECTED_BIT);

        // Start mDNS
        wifi_mdns_start(s_wifi_mgr.device_name);

        // Enable power save
        wifi_power_enable();

        notify_callback(WIFI_MGR_EVENT_CONNECTED);
    }
}
```

**Step 4: Verify build**

```bash
cd ~/claude/boorker-watchdog/firmware
idf.py build
```

Expected: Build succeeds

**Step 5: Commit**

```bash
git add firmware/components/wifi_manager/src/
git commit -m "feat(wifi_manager): implement power management

- Add wifi_power_internal.h for internal power API
- Implement wifi_power.c with modem sleep mode
- Enable power save automatically after connection
- Respect CONFIG_WIFI_MGR_ENABLE_POWER_SAVE setting"
```

---

## Task 6: Integrate into Main Application

**Files:**
- Modify: `firmware/main/main.c`
- Modify: `firmware/main/CMakeLists.txt`

**Step 1: Update main CMakeLists.txt**

Replace `firmware/main/CMakeLists.txt`:

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES wifi_manager
)
```

**Step 2: Update main.c**

Replace `firmware/main/main.c`:

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

#include "wifi_manager.h"

static const char *TAG = "boorker";

static void wifi_event_callback(wifi_mgr_event_t event, void *ctx)
{
    switch (event) {
        case WIFI_MGR_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected - ready for services");
            break;

        case WIFI_MGR_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi disconnected - services paused");
            break;

        case WIFI_MGR_EVENT_PROVISIONING:
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "WiFi Provisioning Mode");
            ESP_LOGI(TAG, "1. Install 'ESP BLE Prov' app (iOS/Android)");
            ESP_LOGI(TAG, "2. Scan for BLE device");
            ESP_LOGI(TAG, "3. Enter PIN when prompted");
            ESP_LOGI(TAG, "4. Select WiFi network and enter password");
            ESP_LOGI(TAG, "========================================");
            break;

        case WIFI_MGR_EVENT_PROVISIONED:
            ESP_LOGI(TAG, "Credentials saved - connecting...");
            break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Boorker starting...");

    // Initialize NVS (required for WiFi credential storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "NVS initialized");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Free PSRAM: %lu bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // Initialize WiFi manager
    wifi_mgr_config_t wifi_config = {
        .device_name = "boorker-dev",
        .start_provisioning = false,  // Only if no stored credentials
        .callback = wifi_event_callback,
        .callback_ctx = NULL,
    };

    ret = wifi_mgr_init(&wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi manager init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    EventGroupHandle_t events = wifi_mgr_get_event_group();
    xEventGroupWaitBits(events, WIFI_MGR_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    // Get IP address
    char ip[16];
    if (wifi_mgr_get_ip(ip, sizeof(ip)) == ESP_OK) {
        ESP_LOGI(TAG, "Connected with IP: %s", ip);
    }

    // Main loop - heartbeat
    while (1) {
        ESP_LOGI(TAG, "Heartbeat - state: %s, heap: %lu",
                 wifi_mgr_get_state_name(),
                 esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
```

**Step 3: Verify build**

```bash
cd ~/claude/boorker-watchdog/firmware
idf.py build
```

Expected: Build succeeds with wifi_manager integrated

**Step 4: Commit**

```bash
git add firmware/main/
git commit -m "feat: integrate wifi_manager into main application

- Add wifi_manager to main CMakeLists.txt REQUIRES
- Update main.c to use wifi_manager API
- Add wifi_event_callback for state notifications
- Display provisioning instructions on PROVISIONING event
- Wait for WIFI_MGR_CONNECTED_BIT before proceeding"
```

---

## Task 7: Device Testing

**Files:**
- None (manual testing)

**Step 1: Flash firmware**

```bash
source ~/esp/esp-idf/export.sh
cd ~/claude/boorker-watchdog/firmware
idf.py build
/esp:flash
```

**Step 2: Monitor serial output**

```bash
/esp:monitor
```

Expected output (first boot, no stored credentials):
```
I (xxx) boorker: Boorker starting...
I (xxx) boorker: NVS initialized
I (xxx) wifi_manager: Initializing WiFi manager...
I (xxx) wifi_manager: No stored credentials, starting provisioning...
I (xxx) wifi_prov: Starting BLE provisioning as 'boorker-dev'...
I (xxx) wifi_prov: BLE provisioning active. Use ESP BLE Prov app to configure.
I (xxx) wifi_prov: Proof-of-Possession PIN: boorker123
I (xxx) boorker: WiFi Provisioning Mode
```

**Step 3: Provision via mobile app**

1. Install "ESP BLE Prov" app on iOS or Android
2. Open app and scan for BLE devices
3. Select "boorker-dev"
4. Enter PIN: `boorker123`
5. Select WiFi network and enter password
6. Wait for confirmation

Expected serial output after provisioning:
```
I (xxx) wifi_prov: Received credentials for SSID: YourNetwork
I (xxx) wifi_prov: Provisioning successful
I (xxx) wifi_manager: Credentials received successfully
I (xxx) boorker: Credentials saved - connecting...
I (xxx) wifi_manager: State: PROVISIONING -> CONNECTING
I (xxx) wifi_manager: Got IP: 192.168.1.xxx
I (xxx) wifi_mdns: Starting mDNS as 'boorker-dev.local'...
I (xxx) wifi_power: Enabling WiFi power save (modem sleep)...
I (xxx) boorker: WiFi connected - ready for services
I (xxx) boorker: Connected with IP: 192.168.1.xxx
```

**Step 4: Test mDNS discovery**

From another device on the same network:
```bash
ping boorker-dev.local
```

Expected: Ping succeeds

**Step 5: Test reconnection**

1. Power cycle the ESP32 (press RST)
2. Observe serial output

Expected:
```
I (xxx) wifi_manager: Found stored credentials, connecting...
I (xxx) wifi_manager: State: IDLE -> CONNECTING
I (xxx) wifi_manager: Got IP: 192.168.1.xxx
```

**Step 6: Test exponential backoff**

1. Disconnect WiFi router briefly
2. Observe serial output

Expected:
```
W (xxx) wifi_manager: Disconnected from AP (reason: 200)
I (xxx) wifi_manager: State: CONNECTED -> RECONNECTING
I (xxx) wifi_manager: Reconnecting in 1000 ms...
I (xxx) wifi_manager: Attempting reconnection...
I (xxx) wifi_manager: Reconnecting in 2000 ms...
...
```

**Step 7: Commit final state**

```bash
git add -A
git status  # Verify only expected changes
git commit -m "test: verify wifi_manager on device

Tested:
- First boot provisioning via ESP BLE Prov app
- Stored credential auto-connect on reboot
- mDNS discovery (boorker-dev.local)
- Exponential backoff reconnection
- Power save mode enabled"
```

---

## Summary

| Task | Description | Files |
|------|-------------|-------|
| 1 | Component structure | CMakeLists.txt, Kconfig, headers, stubs |
| 2 | Core state machine | wifi_manager.c |
| 3 | BLE provisioning | wifi_provisioning.c, sdkconfig.defaults |
| 4 | mDNS discovery | wifi_mdns.c |
| 5 | Power management | wifi_power.c |
| 6 | Main integration | main.c, main/CMakeLists.txt |
| 7 | Device testing | Manual verification |

**Total estimated lines:** ~715 (per design doc)

**Post-implementation:**
- Update setup plan Phase 2 to mark complete
- Document any issues discovered during testing
- Plan next phase (MicroLink/Tailscale integration)

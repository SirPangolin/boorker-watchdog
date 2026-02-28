# Tailscale Manager Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement a tailscale_manager ESP-IDF component that wraps MicroLink for Tailscale VPN connectivity with NVS auth key storage and serial console provisioning.

**Architecture:** Component-based design matching wifi_manager pattern. MicroLink added as git submodule, wrapped by tailscale_manager which provides event callbacks, NVS storage, and console commands. Update task polls MicroLink; state machine handles connection lifecycle.

**Tech Stack:** ESP-IDF v5.5.3, MicroLink (git submodule), FreeRTOS, NVS, esp_console

**Design Document:** See `docs/plans/2026-02-28-tailscale-manager-design.md`

---

## Prerequisites

- WiFi manager component complete (v0.1.0)
- ESP-IDF v5.5.3 environment configured
- Device provisioned with WiFi credentials

---

## Task 1: Add MicroLink Submodule

**Files:**
- Create: `firmware/components/microlink/` (git submodule)
- Modify: `.gitmodules`

**Step 1: Add MicroLink as git submodule**

```bash
cd ~/claude/boorker-watchdog
git submodule add https://github.com/CamM2325/microlink.git firmware/components/microlink
```

**Step 2: Verify submodule added**

```bash
ls firmware/components/microlink/
```

Expected: Should see MicroLink source files (CMakeLists.txt, include/, src/, etc.)

**Step 3: Update sdkconfig.defaults for MicroLink requirements**

Add to `firmware/sdkconfig.defaults`:

```
# MicroLink/Tailscale Requirements
CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE=y

# TLS Configuration
CONFIG_MBEDTLS_SSL_MAX_CONTENT_LEN=16384
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_FULL=y

# Network stack
CONFIG_LWIP_IP4_FRAG=y
CONFIG_LWIP_IP4_REASSEMBLY=y
CONFIG_LWIP_IP6_FRAG=y
CONFIG_LWIP_IP6_REASSEMBLY=y

# HTTP client (for Tailscale coordination)
CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS=y
```

**Step 4: Clean and rebuild to verify MicroLink compiles**

```bash
cd firmware
rm -rf build sdkconfig
source ~/esp/esp-idf/export.sh
idf.py set-target esp32s3
idf.py build
```

Expected: Build succeeds (MicroLink component discovered and compiled)

**Step 5: Commit**

```bash
cd ~/claude/boorker-watchdog
git add .gitmodules firmware/components/microlink firmware/sdkconfig.defaults
git commit -m "feat: add MicroLink submodule for Tailscale VPN

- Add microlink as git submodule
- Update sdkconfig.defaults for TLS, network stack requirements
- Verify build with MicroLink component"
```

---

## Task 2: Scaffold tailscale_manager Component

**Files:**
- Create: `firmware/components/tailscale_manager/CMakeLists.txt`
- Create: `firmware/components/tailscale_manager/Kconfig`
- Create: `firmware/components/tailscale_manager/include/tailscale_manager.h`
- Create: `firmware/components/tailscale_manager/src/tailscale_manager.c` (stub)
- Create: `firmware/components/tailscale_manager/src/tailscale_nvs.h`
- Create: `firmware/components/tailscale_manager/src/tailscale_nvs.c` (stub)
- Create: `firmware/components/tailscale_manager/src/tailscale_console.h`
- Create: `firmware/components/tailscale_manager/src/tailscale_console.c` (stub)

**Step 1: Create CMakeLists.txt**

```cmake
idf_component_register(
    SRCS
        "src/tailscale_manager.c"
        "src/tailscale_nvs.c"
        "src/tailscale_console.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_console esp_event esp_timer log microlink
    PRIV_REQUIRES freertos
)
```

**Step 2: Create Kconfig**

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
            How often to call microlink_update().

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

**Step 3: Create public header (tailscale_manager.h)**

```c
#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tailscale manager event types
 */
typedef enum {
    TS_MGR_EVENT_CONNECTED,           ///< Got Tailscale IP
    TS_MGR_EVENT_DISCONNECTED,        ///< Lost connection, will retry
    TS_MGR_EVENT_UNCONFIGURED,        ///< No auth key in NVS
    TS_MGR_EVENT_AUTH_FAILED,         ///< Invalid/expired auth key
    TS_MGR_EVENT_RECONNECT_EXHAUSTED, ///< Max reconnection attempts reached
    TS_MGR_EVENT_KEY_UPDATED,         ///< New auth key set
} ts_mgr_event_t;

/**
 * @brief Callback function signature for Tailscale events
 */
typedef void (*ts_mgr_callback_t)(ts_mgr_event_t event, void *ctx);

/**
 * @brief Tailscale manager configuration
 */
typedef struct {
    const char *device_name;       ///< Tailscale device name (NULL = use Kconfig)
    ts_mgr_callback_t callback;    ///< Event callback (optional)
    void *callback_ctx;            ///< Context passed to callback
} ts_mgr_config_t;

/**
 * @brief Initialize Tailscale manager
 *
 * Checks NVS for stored auth key. If found, begins connection.
 * If not found, fires TS_MGR_EVENT_UNCONFIGURED.
 * Requires WiFi to be connected first.
 *
 * @param config Configuration (can be NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t ts_mgr_init(const ts_mgr_config_t *config);

/**
 * @brief Stop Tailscale manager and cleanup
 * @return ESP_OK on success
 */
esp_err_t ts_mgr_stop(void);

/**
 * @brief Check if connected to Tailscale
 * @return true if connected with Tailscale IP
 */
bool ts_mgr_is_connected(void);

/**
 * @brief Check if auth key is configured
 * @return true if auth key exists in NVS
 */
bool ts_mgr_is_configured(void);

/**
 * @brief Get current state name for logging
 * @return State name string
 */
const char* ts_mgr_get_state_name(void);

/**
 * @brief Get Tailscale VPN IP address
 * @param buf Buffer to write IP string
 * @param len Buffer length (minimum 16 bytes)
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not connected
 */
esp_err_t ts_mgr_get_ip(char *buf, size_t len);

/**
 * @brief Set Tailscale auth key (stores in NVS)
 *
 * Validates key format, stores in NVS, and triggers connection.
 * Key is never logged for security.
 *
 * @param key Auth key (must start with "tskey-auth-")
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if format invalid
 */
esp_err_t ts_mgr_set_auth_key(const char *key);

/**
 * @brief Clear stored auth key
 * @return ESP_OK on success
 */
esp_err_t ts_mgr_clear_auth_key(void);

/**
 * @brief Check if auth key exists in NVS
 * @return true if key is stored
 */
bool ts_mgr_has_auth_key(void);

/**
 * @brief Register console commands (ts_auth, ts_clear, ts_status)
 *
 * Call after esp_console is initialized.
 *
 * @return ESP_OK on success
 */
esp_err_t ts_console_register(void);

#ifdef __cplusplus
}
#endif
```

**Step 4: Create stub implementation files**

`src/tailscale_manager.c`:
```c
#include "tailscale_manager.h"
#include "esp_log.h"

static const char *TAG = "tailscale_mgr";

esp_err_t ts_mgr_init(const ts_mgr_config_t *config)
{
    ESP_LOGI(TAG, "Tailscale manager init (stub)");
    return ESP_OK;
}

esp_err_t ts_mgr_stop(void)
{
    return ESP_OK;
}

bool ts_mgr_is_connected(void)
{
    return false;
}

bool ts_mgr_is_configured(void)
{
    return false;
}

const char* ts_mgr_get_state_name(void)
{
    return "STUB";
}

esp_err_t ts_mgr_get_ip(char *buf, size_t len)
{
    return ESP_ERR_INVALID_STATE;
}

esp_err_t ts_mgr_set_auth_key(const char *key)
{
    return ESP_OK;
}

esp_err_t ts_mgr_clear_auth_key(void)
{
    return ESP_OK;
}

bool ts_mgr_has_auth_key(void)
{
    return false;
}
```

`src/tailscale_nvs.h`:
```c
#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t ts_nvs_store_key(const char *key);
esp_err_t ts_nvs_load_key(char *buf, size_t buf_len);
esp_err_t ts_nvs_clear_key(void);
bool ts_nvs_has_key(void);
```

`src/tailscale_nvs.c`:
```c
#include "tailscale_nvs.h"

esp_err_t ts_nvs_store_key(const char *key)
{
    return ESP_OK;
}

esp_err_t ts_nvs_load_key(char *buf, size_t buf_len)
{
    return ESP_ERR_NOT_FOUND;
}

esp_err_t ts_nvs_clear_key(void)
{
    return ESP_OK;
}

bool ts_nvs_has_key(void)
{
    return false;
}
```

`src/tailscale_console.h`:
```c
#pragma once

#include "esp_err.h"

esp_err_t ts_console_register(void);
```

`src/tailscale_console.c`:
```c
#include "tailscale_console.h"
#include "esp_log.h"

static const char *TAG = "ts_console";

esp_err_t ts_console_register(void)
{
    ESP_LOGI(TAG, "Console commands registered (stub)");
    return ESP_OK;
}
```

**Step 5: Build to verify component compiles**

```bash
cd firmware
idf.py build
```

Expected: Build succeeds with tailscale_manager component

**Step 6: Commit**

```bash
git add firmware/components/tailscale_manager/
git commit -m "feat(tailscale_manager): scaffold component structure

- Add CMakeLists.txt with dependencies
- Add Kconfig options (device name, timeouts, task config)
- Add public API header with Doxygen docs
- Add stub implementations for all functions"
```

---

## Task 3: Implement NVS Storage

**Files:**
- Modify: `firmware/components/tailscale_manager/src/tailscale_nvs.c`

**Step 1: Implement NVS functions**

Replace `src/tailscale_nvs.c`:

```c
#include "tailscale_nvs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ts_nvs";

#define NVS_NAMESPACE "tailscale"
#define NVS_KEY_AUTH  "auth_key"

esp_err_t ts_nvs_store_key(const char *key)
{
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_str(handle, NVS_KEY_AUTH, key);
    if (ret == ESP_OK) {
        ret = nvs_commit(handle);
    }

    nvs_close(handle);

    if (ret == ESP_OK) {
        // Never log the key itself - only length for debugging
        ESP_LOGI(TAG, "Auth key stored (length: %d)", strlen(key));
    } else {
        ESP_LOGE(TAG, "Failed to store auth key: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t ts_nvs_load_key(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        // NVS namespace doesn't exist yet - not an error
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            return ESP_ERR_NOT_FOUND;
        }
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t len = buf_len;
    ret = nvs_get_str(handle, NVS_KEY_AUTH, buf, &len);
    nvs_close(handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Auth key loaded (length: %d)", strlen(buf));
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to load auth key: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t ts_nvs_clear_key(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            return ESP_OK;  // Nothing to clear
        }
        return ret;
    }

    ret = nvs_erase_key(handle, NVS_KEY_AUTH);
    if (ret == ESP_OK || ret == ESP_ERR_NVS_NOT_FOUND) {
        nvs_commit(handle);
        ret = ESP_OK;
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Auth key cleared");
    return ret;
}

bool ts_nvs_has_key(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return false;
    }

    size_t len = 0;
    ret = nvs_get_str(handle, NVS_KEY_AUTH, NULL, &len);
    nvs_close(handle);

    return (ret == ESP_OK && len > 0);
}
```

**Step 2: Build to verify**

```bash
idf.py build
```

Expected: Build succeeds

**Step 3: Commit**

```bash
git add firmware/components/tailscale_manager/src/tailscale_nvs.c
git commit -m "feat(tailscale_manager): implement NVS auth key storage

- Store/load auth key from NVS 'tailscale' namespace
- Never log auth key contents (security)
- Handle missing namespace gracefully"
```

---

## Task 4: Implement Core State Machine

**Files:**
- Modify: `firmware/components/tailscale_manager/src/tailscale_manager.c`

**Step 1: Implement full state machine**

Replace `src/tailscale_manager.c`:

```c
#include "tailscale_manager.h"
#include "tailscale_nvs.h"
#include "sdkconfig.h"

#include "microlink.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include <string.h>

static const char *TAG = "tailscale_mgr";

// Auth key buffer size (Tailscale keys are ~60 chars)
#define AUTH_KEY_MAX_LEN 128

// Internal state machine
typedef enum {
    TS_MGR_STATE_IDLE,
    TS_MGR_STATE_UNCONFIGURED,
    TS_MGR_STATE_CONNECTING,
    TS_MGR_STATE_CONNECTED,
    TS_MGR_STATE_RECONNECTING,
} ts_mgr_state_t;

// Internal state
static struct {
    ts_mgr_state_t state;
    SemaphoreHandle_t state_mutex;
    microlink_t *ml_handle;
    TaskHandle_t update_task;
    ts_mgr_callback_t callback;
    void *callback_ctx;
    const char *device_name;
    uint8_t reconnect_attempts;
    uint32_t reconnect_delay_ms;
    char auth_key[AUTH_KEY_MAX_LEN];
    bool stop_requested;
} s_ts_mgr = {
    .state = TS_MGR_STATE_IDLE,
    .reconnect_delay_ms = CONFIG_TS_MGR_RECONNECT_INITIAL_MS,
};

// State name lookup
static const char *state_names[] = {
    [TS_MGR_STATE_IDLE] = "IDLE",
    [TS_MGR_STATE_UNCONFIGURED] = "UNCONFIGURED",
    [TS_MGR_STATE_CONNECTING] = "CONNECTING",
    [TS_MGR_STATE_CONNECTED] = "CONNECTED",
    [TS_MGR_STATE_RECONNECTING] = "RECONNECTING",
};

// Forward declarations
static void update_task(void *arg);
static void set_state(ts_mgr_state_t new_state);
static void notify_callback(ts_mgr_event_t event);
static esp_err_t start_microlink(void);
static void handle_reconnect(void);

esp_err_t ts_mgr_init(const ts_mgr_config_t *config)
{
    ESP_LOGI(TAG, "Initializing Tailscale manager...");

    // Create state mutex
    s_ts_mgr.state_mutex = xSemaphoreCreateMutex();
    if (s_ts_mgr.state_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }

    // Store config
    if (config) {
        s_ts_mgr.callback = config->callback;
        s_ts_mgr.callback_ctx = config->callback_ctx;
        s_ts_mgr.device_name = config->device_name ? config->device_name
                                                   : CONFIG_TS_MGR_DEVICE_NAME;
    } else {
        s_ts_mgr.device_name = CONFIG_TS_MGR_DEVICE_NAME;
    }

    s_ts_mgr.stop_requested = false;

    // Check for stored auth key
    esp_err_t ret = ts_nvs_load_key(s_ts_mgr.auth_key, sizeof(s_ts_mgr.auth_key));
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Found stored auth key, connecting...");
        ret = start_microlink();
        if (ret != ESP_OK) {
            return ret;
        }
    } else {
        ESP_LOGI(TAG, "No stored auth key, waiting for configuration...");
        set_state(TS_MGR_STATE_UNCONFIGURED);
        notify_callback(TS_MGR_EVENT_UNCONFIGURED);
    }

    // Create update task
    BaseType_t task_ret = xTaskCreate(
        update_task,
        "ts_update",
        CONFIG_TS_MGR_TASK_STACK_SIZE,
        NULL,
        CONFIG_TS_MGR_TASK_PRIORITY,
        &s_ts_mgr.update_task
    );

    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create update task");
        if (s_ts_mgr.ml_handle) {
            microlink_deinit(s_ts_mgr.ml_handle);
            s_ts_mgr.ml_handle = NULL;
        }
        vSemaphoreDelete(s_ts_mgr.state_mutex);
        s_ts_mgr.state_mutex = NULL;
        return ESP_FAIL;
    }

    return ESP_OK;
}

static esp_err_t start_microlink(void)
{
    microlink_config_t ml_config;
    microlink_get_default_config(&ml_config);
    ml_config.auth_key = s_ts_mgr.auth_key;
    ml_config.device_name = s_ts_mgr.device_name;

    s_ts_mgr.ml_handle = microlink_init(&ml_config);
    if (s_ts_mgr.ml_handle == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MicroLink");
        return ESP_FAIL;
    }

    set_state(TS_MGR_STATE_CONNECTING);
    ESP_LOGI(TAG, "Connecting to Tailscale as '%s'...", s_ts_mgr.device_name);
    microlink_connect(s_ts_mgr.ml_handle);

    return ESP_OK;
}

static void update_task(void *arg)
{
    bool was_connected = false;

    while (!s_ts_mgr.stop_requested) {
        if (s_ts_mgr.ml_handle != NULL &&
            s_ts_mgr.state != TS_MGR_STATE_UNCONFIGURED) {

            microlink_update(s_ts_mgr.ml_handle);

            bool is_connected = microlink_is_connected(s_ts_mgr.ml_handle);

            xSemaphoreTake(s_ts_mgr.state_mutex, portMAX_DELAY);

            if (is_connected && !was_connected) {
                // Just connected
                s_ts_mgr.state = TS_MGR_STATE_CONNECTED;
                s_ts_mgr.reconnect_attempts = 0;
                s_ts_mgr.reconnect_delay_ms = CONFIG_TS_MGR_RECONNECT_INITIAL_MS;
                xSemaphoreGive(s_ts_mgr.state_mutex);

                ESP_LOGI(TAG, "State: %s -> CONNECTED", state_names[s_ts_mgr.state]);
                notify_callback(TS_MGR_EVENT_CONNECTED);
            } else if (!is_connected && was_connected) {
                // Just disconnected
                s_ts_mgr.state = TS_MGR_STATE_RECONNECTING;
                xSemaphoreGive(s_ts_mgr.state_mutex);

                ESP_LOGW(TAG, "State: CONNECTED -> RECONNECTING");
                notify_callback(TS_MGR_EVENT_DISCONNECTED);
                handle_reconnect();
            } else {
                xSemaphoreGive(s_ts_mgr.state_mutex);
            }

            was_connected = is_connected;
        }

        vTaskDelay(pdMS_TO_TICKS(CONFIG_TS_MGR_UPDATE_INTERVAL_MS));
    }

    vTaskDelete(NULL);
}

static void handle_reconnect(void)
{
    s_ts_mgr.reconnect_attempts++;

#if CONFIG_TS_MGR_RECONNECT_MAX_ATTEMPTS > 0
    if (s_ts_mgr.reconnect_attempts >= CONFIG_TS_MGR_RECONNECT_MAX_ATTEMPTS) {
        ESP_LOGW(TAG, "Max reconnection attempts (%d) reached",
                 CONFIG_TS_MGR_RECONNECT_MAX_ATTEMPTS);
        notify_callback(TS_MGR_EVENT_RECONNECT_EXHAUSTED);
        return;
    }
#endif

    ESP_LOGI(TAG, "Reconnecting in %lu ms (attempt %d)...",
             s_ts_mgr.reconnect_delay_ms, s_ts_mgr.reconnect_attempts);

    vTaskDelay(pdMS_TO_TICKS(s_ts_mgr.reconnect_delay_ms));

    // Exponential backoff
    s_ts_mgr.reconnect_delay_ms *= 2;
    if (s_ts_mgr.reconnect_delay_ms > CONFIG_TS_MGR_RECONNECT_MAX_MS) {
        s_ts_mgr.reconnect_delay_ms = CONFIG_TS_MGR_RECONNECT_MAX_MS;
    }

    if (s_ts_mgr.ml_handle) {
        set_state(TS_MGR_STATE_CONNECTING);
        microlink_connect(s_ts_mgr.ml_handle);
    }
}

static void set_state(ts_mgr_state_t new_state)
{
    xSemaphoreTake(s_ts_mgr.state_mutex, portMAX_DELAY);
    if (s_ts_mgr.state != new_state) {
        ESP_LOGI(TAG, "State: %s -> %s",
                 state_names[s_ts_mgr.state],
                 state_names[new_state]);
        s_ts_mgr.state = new_state;
    }
    xSemaphoreGive(s_ts_mgr.state_mutex);
}

static void notify_callback(ts_mgr_event_t event)
{
    if (s_ts_mgr.callback) {
        s_ts_mgr.callback(event, s_ts_mgr.callback_ctx);
    }
}

esp_err_t ts_mgr_stop(void)
{
    ESP_LOGI(TAG, "Stopping Tailscale manager...");

    s_ts_mgr.stop_requested = true;

    // Wait for task to stop
    if (s_ts_mgr.update_task) {
        vTaskDelay(pdMS_TO_TICKS(CONFIG_TS_MGR_UPDATE_INTERVAL_MS * 2));
        s_ts_mgr.update_task = NULL;
    }

    // Disconnect and deinit MicroLink
    if (s_ts_mgr.ml_handle) {
        microlink_disconnect(s_ts_mgr.ml_handle);
        microlink_deinit(s_ts_mgr.ml_handle);
        s_ts_mgr.ml_handle = NULL;
    }

    // Clean up mutex
    if (s_ts_mgr.state_mutex) {
        vSemaphoreDelete(s_ts_mgr.state_mutex);
        s_ts_mgr.state_mutex = NULL;
    }

    s_ts_mgr.state = TS_MGR_STATE_IDLE;
    memset(s_ts_mgr.auth_key, 0, sizeof(s_ts_mgr.auth_key));

    return ESP_OK;
}

bool ts_mgr_is_connected(void)
{
    return s_ts_mgr.state == TS_MGR_STATE_CONNECTED;
}

bool ts_mgr_is_configured(void)
{
    return ts_nvs_has_key();
}

const char* ts_mgr_get_state_name(void)
{
    return state_names[s_ts_mgr.state];
}

esp_err_t ts_mgr_get_ip(char *buf, size_t len)
{
    if (buf == NULL || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!ts_mgr_is_connected() || s_ts_mgr.ml_handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    microlink_get_vpn_ip(s_ts_mgr.ml_handle, buf, len);
    return ESP_OK;
}

esp_err_t ts_mgr_set_auth_key(const char *key)
{
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Validate key format
    if (strncmp(key, "tskey-auth-", 11) != 0) {
        ESP_LOGE(TAG, "Invalid auth key format (must start with 'tskey-auth-')");
        return ESP_ERR_INVALID_ARG;
    }

    // Store in NVS
    esp_err_t ret = ts_nvs_store_key(key);
    if (ret != ESP_OK) {
        return ret;
    }

    // Copy to internal buffer
    strncpy(s_ts_mgr.auth_key, key, sizeof(s_ts_mgr.auth_key) - 1);
    s_ts_mgr.auth_key[sizeof(s_ts_mgr.auth_key) - 1] = '\0';

    notify_callback(TS_MGR_EVENT_KEY_UPDATED);

    // If we were unconfigured, start connecting
    if (s_ts_mgr.state == TS_MGR_STATE_UNCONFIGURED) {
        ret = start_microlink();
    }

    return ret;
}

esp_err_t ts_mgr_clear_auth_key(void)
{
    esp_err_t ret = ts_nvs_clear_key();
    memset(s_ts_mgr.auth_key, 0, sizeof(s_ts_mgr.auth_key));

    if (s_ts_mgr.ml_handle) {
        microlink_disconnect(s_ts_mgr.ml_handle);
        microlink_deinit(s_ts_mgr.ml_handle);
        s_ts_mgr.ml_handle = NULL;
    }

    set_state(TS_MGR_STATE_UNCONFIGURED);
    notify_callback(TS_MGR_EVENT_UNCONFIGURED);

    return ret;
}

bool ts_mgr_has_auth_key(void)
{
    return ts_nvs_has_key();
}
```

**Step 2: Build to verify**

```bash
idf.py build
```

Expected: Build succeeds

**Step 3: Commit**

```bash
git add firmware/components/tailscale_manager/src/tailscale_manager.c
git commit -m "feat(tailscale_manager): implement core state machine

- Init checks NVS for auth key, connects or fires UNCONFIGURED
- Update task polls microlink_update() and detects state changes
- Thread-safe state transitions with mutex
- Exponential backoff reconnection with max attempts
- set_auth_key validates format, stores in NVS, triggers connect
- Complete resource cleanup in stop()"
```

---

## Task 5: Implement Console Commands

**Files:**
- Modify: `firmware/components/tailscale_manager/src/tailscale_console.c`

**Step 1: Implement console commands**

Replace `src/tailscale_console.c`:

```c
#include "tailscale_console.h"
#include "tailscale_manager.h"
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include <string.h>

static const char *TAG = "ts_console";

// ts_auth command
static struct {
    struct arg_str *key;
    struct arg_end *end;
} ts_auth_args;

static int cmd_ts_auth(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&ts_auth_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, ts_auth_args.end, argv[0]);
        return 1;
    }

    const char *key = ts_auth_args.key->sval[0];
    if (key == NULL || strlen(key) == 0) {
        printf("Usage: ts_auth <tskey-auth-xxxxx>\n");
        return 1;
    }

    esp_err_t ret = ts_mgr_set_auth_key(key);
    if (ret == ESP_OK) {
        printf("Auth key stored. Connecting to Tailscale...\n");
    } else if (ret == ESP_ERR_INVALID_ARG) {
        printf("Error: Invalid key format. Must start with 'tskey-auth-'\n");
    } else {
        printf("Error: %s\n", esp_err_to_name(ret));
    }

    return ret == ESP_OK ? 0 : 1;
}

// ts_clear command
static int cmd_ts_clear(int argc, char **argv)
{
    esp_err_t ret = ts_mgr_clear_auth_key();
    if (ret == ESP_OK) {
        printf("Auth key cleared. Tailscale disabled.\n");
    } else {
        printf("Error: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

// ts_status command
static int cmd_ts_status(int argc, char **argv)
{
    printf("Tailscale Status:\n");
    printf("  State: %s\n", ts_mgr_get_state_name());
    printf("  Configured: %s\n", ts_mgr_has_auth_key() ? "yes" : "no");

    if (ts_mgr_is_connected()) {
        char ip[16];
        if (ts_mgr_get_ip(ip, sizeof(ip)) == ESP_OK) {
            printf("  Tailscale IP: %s\n", ip);
        }
    }

    return 0;
}

esp_err_t ts_console_register(void)
{
    // ts_auth command
    ts_auth_args.key = arg_str1(NULL, NULL, "<key>", "Tailscale auth key");
    ts_auth_args.end = arg_end(1);

    const esp_console_cmd_t ts_auth_cmd = {
        .command = "ts_auth",
        .help = "Set Tailscale auth key (stored in NVS)",
        .hint = NULL,
        .func = &cmd_ts_auth,
        .argtable = &ts_auth_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ts_auth_cmd));

    // ts_clear command
    const esp_console_cmd_t ts_clear_cmd = {
        .command = "ts_clear",
        .help = "Clear Tailscale auth key",
        .hint = NULL,
        .func = &cmd_ts_clear,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ts_clear_cmd));

    // ts_status command
    const esp_console_cmd_t ts_status_cmd = {
        .command = "ts_status",
        .help = "Show Tailscale connection status",
        .hint = NULL,
        .func = &cmd_ts_status,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&ts_status_cmd));

    ESP_LOGI(TAG, "Registered commands: ts_auth, ts_clear, ts_status");
    return ESP_OK;
}
```

**Step 2: Build to verify**

```bash
idf.py build
```

Expected: Build succeeds

**Step 3: Commit**

```bash
git add firmware/components/tailscale_manager/src/tailscale_console.c
git commit -m "feat(tailscale_manager): implement console commands

- ts_auth <key>: Set auth key with format validation
- ts_clear: Clear stored key and disconnect
- ts_status: Show state, configured status, and IP"
```

---

## Task 6: Integrate into Main Application

**Files:**
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/main/main.c`

**Step 1: Update CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES wifi_manager tailscale_manager esp_console nvs_flash
)
```

**Step 2: Update main.c with Tailscale integration**

```c
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_console.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "tailscale_manager.h"

static const char *TAG = "boorker";

static void tailscale_callback(ts_mgr_event_t event, void *ctx)
{
    char ip[16];
    switch (event) {
        case TS_MGR_EVENT_CONNECTED:
            if (ts_mgr_get_ip(ip, sizeof(ip)) == ESP_OK) {
                ESP_LOGI(TAG, "Tailscale connected: %s", ip);
            }
            break;

        case TS_MGR_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Tailscale disconnected - reconnecting...");
            break;

        case TS_MGR_EVENT_UNCONFIGURED:
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "Tailscale not configured");
            ESP_LOGI(TAG, "Use 'ts_auth <key>' to set auth key");
            ESP_LOGI(TAG, "Get key from: https://login.tailscale.com/admin/settings/keys");
            ESP_LOGI(TAG, "========================================");
            break;

        case TS_MGR_EVENT_AUTH_FAILED:
            ESP_LOGW(TAG, "Tailscale auth failed - key may be expired");
            break;

        case TS_MGR_EVENT_RECONNECT_EXHAUSTED:
            ESP_LOGW(TAG, "Tailscale reconnection exhausted");
            break;

        case TS_MGR_EVENT_KEY_UPDATED:
            ESP_LOGI(TAG, "Tailscale auth key updated");
            break;
    }
}

static void wifi_event_callback(wifi_mgr_event_t event, void *ctx)
{
    switch (event) {
        case WIFI_MGR_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected - initializing Tailscale...");

            // Initialize Tailscale after WiFi is connected
            ts_mgr_config_t ts_config = {
                .device_name = "boorker-dev",
                .callback = tailscale_callback,
                .callback_ctx = NULL,
            };
            ts_mgr_init(&ts_config);
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

        case WIFI_MGR_EVENT_RECONNECT_EXHAUSTED:
            ESP_LOGW(TAG, "WiFi reconnection attempts exhausted");
            break;
    }
}

static void init_console(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "boorker>";
    repl_config.max_cmdline_length = 256;

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    // Register Tailscale console commands
    ts_console_register();

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    ESP_LOGI(TAG, "Console ready. Type 'help' for commands.");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Boorker starting...");

    // Initialize NVS (required for WiFi and Tailscale credential storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "NVS initialized");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Free PSRAM: %lu bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // Initialize console for ts_auth command
    init_console();

    // Initialize WiFi manager (Tailscale init happens in callback)
    wifi_mgr_config_t wifi_config = {
        .device_name = "boorker-dev",
        .start_provisioning = false,
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

    // Get WiFi IP address
    char ip[16];
    if (wifi_mgr_get_ip(ip, sizeof(ip)) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected with IP: %s", ip);
    }

    // Main loop - heartbeat
    while (1) {
        ESP_LOGI(TAG, "Heartbeat - WiFi: %s, Tailscale: %s, heap: %lu",
                 wifi_mgr_get_state_name(),
                 ts_mgr_get_state_name(),
                 esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}
```

**Step 3: Build**

```bash
idf.py build
```

Expected: Build succeeds

**Step 4: Commit**

```bash
git add firmware/main/CMakeLists.txt firmware/main/main.c
git commit -m "feat: integrate tailscale_manager into main application

- Initialize console with ts_auth, ts_clear, ts_status commands
- Initialize Tailscale manager after WiFi connects
- Add Tailscale callback for connection events
- Update heartbeat to show both WiFi and Tailscale state"
```

---

## Task 7: Device Testing

**Test sequence (manual):**

**Step 1: Flash and monitor**

```bash
idf.py -p /dev/ttyUSB0 flash
# Use /esp:monitor skill
```

**Step 2: Verify boot sequence**

Expected logs:
```
I (xxx) boorker: Boorker starting...
I (xxx) boorker: NVS initialized
I (xxx) boorker: Console ready. Type 'help' for commands.
I (xxx) wifi_manager: Found stored credentials, connecting...
I (xxx) wifi_manager: State: IDLE -> CONNECTING
I (xxx) wifi_manager: Got IP: 192.168.x.x
I (xxx) boorker: WiFi connected - initializing Tailscale...
I (xxx) tailscale_mgr: Initializing Tailscale manager...
I (xxx) tailscale_mgr: No stored auth key, waiting for configuration...
I (xxx) tailscale_mgr: State: IDLE -> UNCONFIGURED
I (xxx) boorker: Tailscale not configured
I (xxx) boorker: Use 'ts_auth <key>' to set auth key
```

**Step 3: Test ts_status command**

In serial monitor, type:
```
ts_status
```

Expected:
```
Tailscale Status:
  State: UNCONFIGURED
  Configured: no
```

**Step 4: Generate Tailscale auth key**

1. Open https://login.tailscale.com/admin/settings/keys
2. Generate auth key (Reusable: Yes, Ephemeral: No)
3. Copy key (starts with `tskey-auth-`)

**Step 5: Test ts_auth command**

In serial monitor:
```
ts_auth tskey-auth-kYOURKEYHERE
```

Expected:
```
Auth key stored. Connecting to Tailscale...
```

Then in logs:
```
I (xxx) tailscale_mgr: State: UNCONFIGURED -> CONNECTING
I (xxx) tailscale_mgr: Connecting to Tailscale as 'boorker-dev'...
... (connection process)
I (xxx) tailscale_mgr: State: CONNECTING -> CONNECTED
I (xxx) boorker: Tailscale connected: 100.x.y.z
```

**Step 6: Verify ts_status shows connection**

```
ts_status
```

Expected:
```
Tailscale Status:
  State: CONNECTED
  Configured: yes
  Tailscale IP: 100.x.y.z
```

**Step 7: Test from another Tailscale device**

```bash
tailscale ping boorker-dev
```

**Step 8: Test key persistence (reboot)**

Press RST button, verify device reconnects automatically with stored key.

**Step 9: Test ts_clear**

```
ts_clear
```

Expected: Device goes back to UNCONFIGURED state.

---

## Summary

| Task | Description | Commit Message |
|------|-------------|----------------|
| 1 | Add MicroLink submodule | `feat: add MicroLink submodule for Tailscale VPN` |
| 2 | Scaffold component | `feat(tailscale_manager): scaffold component structure` |
| 3 | Implement NVS storage | `feat(tailscale_manager): implement NVS auth key storage` |
| 4 | Implement state machine | `feat(tailscale_manager): implement core state machine` |
| 5 | Implement console commands | `feat(tailscale_manager): implement console commands` |
| 6 | Integrate into main | `feat: integrate tailscale_manager into main application` |
| 7 | Device testing | Manual testing |

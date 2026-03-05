/**
 * @file system_state.c
 * @brief System lifecycle state management implementation
 *
 * Manages system lifecycle states with NVS persistence and thread-safe access.
 */

#include "system_state.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

static const char *TAG = "system_state";

// NVS keys
#define NVS_KEY_CLAIMED     "claimed"
#define NVS_KEY_FACTORY_RST "factory_rst"
#define NVS_KEY_OTA_STATE   "ota_state"

// Mutex timeout
#define MUTEX_TIMEOUT_MS        100
#define DEINIT_MUTEX_TIMEOUT_MS 500

// State name arrays for logging
static const char *factory_reset_names[] = {
    [SYSTEM_FACTORY_RESET_NONE]        = "NONE",
    [SYSTEM_FACTORY_RESET_PENDING]     = "PENDING",
    [SYSTEM_FACTORY_RESET_IN_PROGRESS] = "IN_PROGRESS",
};

static const char *ota_state_names[] = {
    [SYSTEM_OTA_IDLE]           = "IDLE",
    [SYSTEM_OTA_DOWNLOADING]    = "DOWNLOADING",
    [SYSTEM_OTA_VERIFYING]      = "VERIFYING",
    [SYSTEM_OTA_PENDING_REBOOT] = "PENDING_REBOOT",
};

// Static state structure
static struct {
    bool initialized;
    bool claimed;
    system_factory_reset_t factory_reset;
    system_ota_state_t ota_state;
    SemaphoreHandle_t mutex;
} s_state = {
    .initialized = false,
    .claimed = false,
    .factory_reset = SYSTEM_FACTORY_RESET_NONE,
    .ota_state = SYSTEM_OTA_IDLE,
    .mutex = NULL,
};

// --------------------------------------------------------------------------
// NVS Helper Functions
// --------------------------------------------------------------------------

/**
 * @brief Load all state from NVS
 *
 * Called during init. Errors are logged but don't fail initialization
 * (defaults are used for missing values).
 */
static void load_state_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(CONFIG_SYSTEM_STATE_NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved state found, using defaults");
        } else {
            ESP_LOGW(TAG, "Failed to open NVS for reading: %s", esp_err_to_name(ret));
        }
        return;
    }

    // Load claimed status
    uint8_t claimed_val;
    ret = nvs_get_u8(handle, NVS_KEY_CLAIMED, &claimed_val);
    if (ret == ESP_OK) {
        s_state.claimed = (claimed_val != 0);
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read '%s': %s", NVS_KEY_CLAIMED, esp_err_to_name(ret));
    }

    // Load factory reset state
    uint8_t factory_rst_val;
    ret = nvs_get_u8(handle, NVS_KEY_FACTORY_RST, &factory_rst_val);
    if (ret == ESP_OK) {
        if (factory_rst_val <= SYSTEM_FACTORY_RESET_IN_PROGRESS) {
            s_state.factory_reset = (system_factory_reset_t)factory_rst_val;
        } else {
            ESP_LOGW(TAG, "Invalid factory_rst value %d, using NONE", factory_rst_val);
            s_state.factory_reset = SYSTEM_FACTORY_RESET_NONE;
        }
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read '%s': %s", NVS_KEY_FACTORY_RST, esp_err_to_name(ret));
    }

    // Load OTA state
    uint8_t ota_state_val;
    ret = nvs_get_u8(handle, NVS_KEY_OTA_STATE, &ota_state_val);
    if (ret == ESP_OK) {
        if (ota_state_val <= SYSTEM_OTA_PENDING_REBOOT) {
            s_state.ota_state = (system_ota_state_t)ota_state_val;
        } else {
            ESP_LOGW(TAG, "Invalid ota_state value %d, using IDLE", ota_state_val);
            s_state.ota_state = SYSTEM_OTA_IDLE;
        }
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read '%s': %s", NVS_KEY_OTA_STATE, esp_err_to_name(ret));
    }

    nvs_close(handle);

    ESP_LOGI(TAG, "Loaded state: claimed=%d, factory_reset=%s, ota=%s",
             s_state.claimed,
             factory_reset_names[s_state.factory_reset],
             ota_state_names[s_state.ota_state]);
}

/**
 * @brief Save a single u8 value to NVS
 *
 * @param key NVS key
 * @param value Value to save
 * @return ESP_OK on success, or NVS error
 */
static esp_err_t save_u8_to_nvs(const char *key, uint8_t value)
{
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(CONFIG_SYSTEM_STATE_NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_u8(handle, key, value);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set '%s': %s", key, esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit '%s': %s", key, esp_err_to_name(ret));
    }

    nvs_close(handle);
    return ret;
}

// --------------------------------------------------------------------------
// Core API Implementation
// --------------------------------------------------------------------------

esp_err_t system_state_init(void)
{
    if (s_state.initialized) {
        ESP_LOGD(TAG, "Already initialized");
        return ESP_OK;
    }

    // Create mutex
    s_state.mutex = xSemaphoreCreateMutex();
    if (s_state.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Load state from NVS (errors logged but don't fail init)
    load_state_from_nvs();

    s_state.initialized = true;
    ESP_LOGI(TAG, "Initialized");

    return ESP_OK;
}

esp_err_t system_state_deinit(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Take mutex before cleanup with longer timeout
    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(DEINIT_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Mutex timeout during deinit");
        return ESP_ERR_TIMEOUT;
    }

    s_state.initialized = false;

    // Give back mutex before deleting
    xSemaphoreGive(s_state.mutex);
    vSemaphoreDelete(s_state.mutex);
    s_state.mutex = NULL;

    ESP_LOGI(TAG, "Deinitialized");
    return ESP_OK;
}

// --------------------------------------------------------------------------
// Claimed Status API
// --------------------------------------------------------------------------

bool system_state_is_claimed(void)
{
    if (!s_state.initialized) {
        ESP_LOGW(TAG, "system_state_is_claimed() called before init - returning false");
        return false;
    }
    // Reading a single bool is atomic on ESP32
    return s_state.claimed;
}

esp_err_t system_state_set_claimed(bool claimed)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in set_claimed");
        return ESP_ERR_TIMEOUT;
    }

    // Skip if no change
    if (s_state.claimed == claimed) {
        xSemaphoreGive(s_state.mutex);
        return ESP_OK;
    }

    esp_err_t ret = save_u8_to_nvs(NVS_KEY_CLAIMED, claimed ? 1 : 0);
    if (ret == ESP_OK) {
        s_state.claimed = claimed;
        ESP_LOGI(TAG, "Device %s", claimed ? "claimed" : "unclaimed");
    }

    xSemaphoreGive(s_state.mutex);
    return ret;
}

// --------------------------------------------------------------------------
// Factory Reset API
// --------------------------------------------------------------------------

system_factory_reset_t system_state_get_factory_reset(void)
{
    if (!s_state.initialized) {
        ESP_LOGW(TAG, "system_state_get_factory_reset() called before init - returning NONE");
        return SYSTEM_FACTORY_RESET_NONE;
    }
    // Reading a single enum (backed by int) is atomic on ESP32
    return s_state.factory_reset;
}

esp_err_t system_state_request_factory_reset(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in request_factory_reset");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = save_u8_to_nvs(NVS_KEY_FACTORY_RST, (uint8_t)SYSTEM_FACTORY_RESET_PENDING);
    if (ret == ESP_OK) {
        s_state.factory_reset = SYSTEM_FACTORY_RESET_PENDING;
        ESP_LOGI(TAG, "Factory reset requested (state: %s)",
                 factory_reset_names[s_state.factory_reset]);
    }

    xSemaphoreGive(s_state.mutex);
    return ret;
}

esp_err_t system_state_begin_factory_reset(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in begin_factory_reset");
        return ESP_ERR_TIMEOUT;
    }

    // Only allow transition from PENDING state
    if (s_state.factory_reset != SYSTEM_FACTORY_RESET_PENDING) {
        ESP_LOGW(TAG, "Cannot begin factory reset: current state is %s",
                 factory_reset_names[s_state.factory_reset]);
        xSemaphoreGive(s_state.mutex);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = save_u8_to_nvs(NVS_KEY_FACTORY_RST, (uint8_t)SYSTEM_FACTORY_RESET_IN_PROGRESS);
    if (ret == ESP_OK) {
        s_state.factory_reset = SYSTEM_FACTORY_RESET_IN_PROGRESS;
        ESP_LOGI(TAG, "Factory reset in progress (state: %s)",
                 factory_reset_names[s_state.factory_reset]);
    }

    xSemaphoreGive(s_state.mutex);
    return ret;
}

esp_err_t system_state_clear_factory_reset(void)
{
    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in clear_factory_reset");
        return ESP_ERR_TIMEOUT;
    }

    // Skip if already cleared
    if (s_state.factory_reset == SYSTEM_FACTORY_RESET_NONE) {
        xSemaphoreGive(s_state.mutex);
        return ESP_OK;
    }

    esp_err_t ret = save_u8_to_nvs(NVS_KEY_FACTORY_RST, (uint8_t)SYSTEM_FACTORY_RESET_NONE);
    if (ret == ESP_OK) {
        system_factory_reset_t prev_state = s_state.factory_reset;
        s_state.factory_reset = SYSTEM_FACTORY_RESET_NONE;
        ESP_LOGI(TAG, "Factory reset cleared (was: %s)", factory_reset_names[prev_state]);
    }

    xSemaphoreGive(s_state.mutex);
    return ret;
}

// --------------------------------------------------------------------------
// OTA State API
// --------------------------------------------------------------------------

system_ota_state_t system_state_get_ota(void)
{
    if (!s_state.initialized) {
        ESP_LOGW(TAG, "system_state_get_ota() called before init - returning IDLE");
        return SYSTEM_OTA_IDLE;
    }
    // Reading a single enum (backed by int) is atomic on ESP32
    return s_state.ota_state;
}

esp_err_t system_state_set_ota(system_ota_state_t state)
{
    if (state > SYSTEM_OTA_PENDING_REBOOT) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_state.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_state.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in set_ota");
        return ESP_ERR_TIMEOUT;
    }

    // Skip if no change
    if (s_state.ota_state == state) {
        xSemaphoreGive(s_state.mutex);
        return ESP_OK;
    }

    esp_err_t ret = save_u8_to_nvs(NVS_KEY_OTA_STATE, (uint8_t)state);
    if (ret == ESP_OK) {
        system_ota_state_t prev_state = s_state.ota_state;
        s_state.ota_state = state;
        ESP_LOGI(TAG, "OTA state: %s -> %s",
                 ota_state_names[prev_state], ota_state_names[state]);
    }

    xSemaphoreGive(s_state.mutex);
    return ret;
}

// --------------------------------------------------------------------------
// Utility Functions
// --------------------------------------------------------------------------

const char *system_state_factory_reset_name(system_factory_reset_t state)
{
    if (state > SYSTEM_FACTORY_RESET_IN_PROGRESS) {
        return "UNKNOWN";
    }
    return factory_reset_names[state];
}

const char *system_state_ota_name(system_ota_state_t state)
{
    if (state > SYSTEM_OTA_PENDING_REBOOT) {
        return "UNKNOWN";
    }
    return ota_state_names[state];
}

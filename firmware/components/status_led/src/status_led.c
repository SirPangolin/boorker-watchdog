/**
 * @file status_led.c
 * @brief Status LED core implementation (ANDON channel subscriber)
 *
 * Maps system states to LED patterns via LED using the led_driver component
 * for hardware abstraction. Subscribes to andon_service for state
 * notifications and renders appropriate LED patterns.
 */

#include "status_led.h"
#include "andon_service.h"
#include "led_driver.h"
#include "led_indicator.h"  // For blink_step_t type used in led_patterns.c
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include <string.h>

static const char *TAG = "status_led";

// NVS configuration (keep namespace for backward compatibility)
#define NVS_NAMESPACE "led_cfg"
#define NVS_KEY_ENABLED "enabled"
#define NVS_KEY_BRIGHTNESS "brightness"
#define NVS_KEY_ALERTS_ONLY "alerts_only"

// Mutex timeout
#define MUTEX_TIMEOUT_MS 100
#define DEINIT_MUTEX_TIMEOUT_MS 500  // Longer timeout for deinit (5x normal)

// External pattern array from led_patterns.c
extern blink_step_t const *led_patterns[];

// State names for logging
static const char *state_names[] = {
    [STATUS_LED_ALERT_CRITICAL]       = "ALERT_CRITICAL",
    [STATUS_LED_FIRST_BOOT]           = "FIRST_BOOT",
    [STATUS_LED_WIFI_PROVISIONING]    = "WIFI_PROVISIONING",
    [STATUS_LED_WIFI_RECONNECTING]    = "WIFI_RECONNECTING",
    [STATUS_LED_ALERT_ACTIVE]         = "ALERT_ACTIVE",
    [STATUS_LED_WIFI_CONNECTING]      = "WIFI_CONNECTING",
    [STATUS_LED_TAILSCALE_CONNECTING] = "TAILSCALE_CONNECTING",
    [STATUS_LED_CONNECTED]            = "CONNECTED",
    [STATUS_LED_OFF]                  = "OFF",
};

// Verify state_names array matches enum
_Static_assert(sizeof(state_names) / sizeof(state_names[0]) == STATUS_LED_MAX,
               "state_names array size must match STATUS_LED_MAX");

// Static state structure
static struct {
    bool initialized;
    bool enabled;
    uint8_t brightness;
    bool alerts_only;
    status_led_state_t displayed_state;
    SemaphoreHandle_t mutex;
} s_led = {
    .initialized = false,
    .enabled = true,
    .brightness = CONFIG_STATUS_LED_DEFAULT_BRIGHTNESS,
    .alerts_only = false,
    .displayed_state = STATUS_LED_OFF,
    .mutex = NULL,
};

// --------------------------------------------------------------------------
// NVS Functions
// --------------------------------------------------------------------------

/**
 * @brief Load configuration from NVS
 *
 * Loads enabled, brightness, and alerts_only settings from NVS.
 * If NVS values don't exist or there's an error, keeps defaults.
 */
static void load_config_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved config found, using defaults");
        } else {
            ESP_LOGW(TAG, "Failed to open NVS for reading: %s", esp_err_to_name(ret));
        }
        return;
    }

    // Load enabled state
    uint8_t enabled_val;
    ret = nvs_get_u8(handle, NVS_KEY_ENABLED, &enabled_val);
    if (ret == ESP_OK) {
        s_led.enabled = (enabled_val != 0);
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read '%s' from NVS: %s", NVS_KEY_ENABLED, esp_err_to_name(ret));
    }

    // Load brightness
    uint8_t brightness_val;
    ret = nvs_get_u8(handle, NVS_KEY_BRIGHTNESS, &brightness_val);
    if (ret == ESP_OK) {
        if (brightness_val > 100) {
            ESP_LOGW(TAG, "Brightness value %d from NVS invalid, clamping to 100", brightness_val);
            s_led.brightness = 100;
        } else {
            s_led.brightness = brightness_val;
        }
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read '%s' from NVS: %s", NVS_KEY_BRIGHTNESS, esp_err_to_name(ret));
    }

    // Load alerts_only
    uint8_t alerts_only_val;
    ret = nvs_get_u8(handle, NVS_KEY_ALERTS_ONLY, &alerts_only_val);
    if (ret == ESP_OK) {
        s_led.alerts_only = (alerts_only_val != 0);
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read '%s' from NVS: %s", NVS_KEY_ALERTS_ONLY, esp_err_to_name(ret));
    }

    nvs_close(handle);

    ESP_LOGI(TAG, "Loaded config: enabled=%d, brightness=%d, alerts_only=%d",
             s_led.enabled, s_led.brightness, s_led.alerts_only);
}

esp_err_t status_led_save_config(void)
{
    if (!s_led.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_led.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    // Capture values under mutex protection
    bool enabled = s_led.enabled;
    uint8_t brightness = s_led.brightness;
    bool alerts_only = s_led.alerts_only;

    xSemaphoreGive(s_led.mutex);

    // NVS operations can be done outside mutex (values are captured)
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(ret));
        return ret;
    }

    // Save enabled state
    ret = nvs_set_u8(handle, NVS_KEY_ENABLED, enabled ? 1 : 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save enabled: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    // Save brightness
    ret = nvs_set_u8(handle, NVS_KEY_BRIGHTNESS, brightness);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save brightness: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    // Save alerts_only
    ret = nvs_set_u8(handle, NVS_KEY_ALERTS_ONLY, alerts_only ? 1 : 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save alerts_only: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Config saved to NVS");
    }

    nvs_close(handle);
    return ret;
}

// --------------------------------------------------------------------------
// Helper Functions
// --------------------------------------------------------------------------

/**
 * @brief Check if state should be displayed
 *
 * @param state State to check
 * @return true if state should be shown based on enabled and alerts_only settings
 */
static bool should_show_state(status_led_state_t state)
{
    // If disabled, never show anything
    if (!s_led.enabled) {
        return false;
    }

    // If alerts_only mode, only show alert states
    if (s_led.alerts_only) {
        return (state == STATUS_LED_ALERT_CRITICAL || state == STATUS_LED_ALERT_ACTIVE);
    }

    // Show all states when not in alerts_only mode
    return true;
}

/**
 * @brief Apply a pattern to the LED(s)
 *
 * Must be called with mutex held. Updates the LED via led_driver to display
 * the specified pattern.
 *
 * @param state The LED pattern state to apply
 * @return ESP_OK on success, or error from led_driver functions
 */
static esp_err_t apply_pattern(status_led_state_t state)
{
    esp_err_t ret = ESP_OK;

    // Skip if no change needed
    if (state == s_led.displayed_state) {
        return ESP_OK;
    }

    // Stop currently displayed pattern
    ret = led_driver_stop_pattern((int)s_led.displayed_state);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop pattern %s: %s",
                 state_names[s_led.displayed_state], esp_err_to_name(ret));
    }

    // Determine what to show
    status_led_state_t show_state = state;
    if (!should_show_state(state)) {
        show_state = STATUS_LED_OFF;
    }

    // Start new pattern
    ret = led_driver_start_pattern((int)show_state);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start pattern %s: %s",
                 state_names[show_state], esp_err_to_name(ret));
        return ret;
    }

    s_led.displayed_state = state;
    ESP_LOGD(TAG, "Displaying: %s", state_names[show_state]);

    return ESP_OK;
}

/**
 * @brief Map ANDON state to internal LED pattern
 *
 * @param andon_state ANDON state from callback
 * @return Corresponding status_led_state_t pattern
 */
static status_led_state_t map_andon_to_led_state(andon_state_t andon_state)
{
    switch (andon_state) {
        case ANDON_FIRST_BOOT:           return STATUS_LED_FIRST_BOOT;
        case ANDON_ERROR:                return STATUS_LED_ALERT_CRITICAL;
        case ANDON_WIFI_PROVISIONING:    return STATUS_LED_WIFI_PROVISIONING;
        case ANDON_WIFI_RECONNECTING:    return STATUS_LED_WIFI_RECONNECTING;
        case ANDON_WIFI_CONNECTING:      return STATUS_LED_WIFI_CONNECTING;
        case ANDON_TAILSCALE_CONNECTING: return STATUS_LED_TAILSCALE_CONNECTING;
        case ANDON_CONNECTED:            return STATUS_LED_CONNECTED;
        case ANDON_OFF:                  return STATUS_LED_OFF;
        case ANDON_ALERT_CRITICAL:       return STATUS_LED_ALERT_CRITICAL;
        case ANDON_ALERT_ACTIVE:         return STATUS_LED_ALERT_ACTIVE;
        case ANDON_SENSOR_WARNING:       return STATUS_LED_ALERT_ACTIVE;  // Use same pattern
        default:                         return STATUS_LED_OFF;
    }
}

/**
 * @brief ANDON channel callback - maps ANDON state to LED pattern
 *
 * Called by andon_service when the active state changes. Maps the ANDON
 * state to an internal LED pattern and applies it.
 *
 * @param state New active ANDON state
 * @param ctx User context (unused)
 */
static void andon_callback(andon_state_t state, void *ctx)
{
    (void)ctx;  // Unused

    if (!s_led.initialized) {
        return;
    }

    if (xSemaphoreTake(s_led.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in ANDON callback");
        return;
    }

    // Map ANDON state to internal LED pattern
    status_led_state_t led_state = map_andon_to_led_state(state);

    ESP_LOGI(TAG, "ANDON state: %s -> LED pattern: %s",
             andon_state_name(state), state_names[led_state]);

    // Apply the pattern
    esp_err_t ret = apply_pattern(led_state);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to apply pattern %s: %s",
                 state_names[led_state], esp_err_to_name(ret));
    }

    xSemaphoreGive(s_led.mutex);
}

// --------------------------------------------------------------------------
// Core API Implementation
// --------------------------------------------------------------------------

esp_err_t status_led_init(void)
{
    if (s_led.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Create mutex
    s_led.mutex = xSemaphoreCreateMutex();
    if (s_led.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Load config from NVS (errors logged but don't fail init)
    load_config_from_nvs();

    // Initialize LED driver
    esp_err_t ret = led_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED driver: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_led.mutex);
        s_led.mutex = NULL;
        return ret;
    }

    // Register our patterns with the driver
    ret = led_driver_register_patterns(led_patterns, STATUS_LED_MAX);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register patterns: %s", esp_err_to_name(ret));
        led_driver_deinit();
        vSemaphoreDelete(s_led.mutex);
        s_led.mutex = NULL;
        return ret;
    }

    // Set initial brightness
    ret = led_driver_set_brightness(s_led.brightness);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set initial brightness: %s", esp_err_to_name(ret));
    }

    s_led.initialized = true;
    s_led.displayed_state = STATUS_LED_OFF;

    ESP_LOGI(TAG, "Initialized (brightness=%d%%)", s_led.brightness);

    // Register as ANDON channel
    ret = andon_register_channel("led", andon_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ANDON channel: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Registered as ANDON channel");
    }

    return ESP_OK;
}

esp_err_t status_led_deinit(void)
{
    if (!s_led.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_led.mutex, pdMS_TO_TICKS(DEINIT_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Mutex timeout during deinit");
        return ESP_ERR_TIMEOUT;
    }

    // Stop current pattern
    led_driver_stop_pattern((int)s_led.displayed_state);

    s_led.initialized = false;
    s_led.displayed_state = STATUS_LED_OFF;

    xSemaphoreGive(s_led.mutex);
    vSemaphoreDelete(s_led.mutex);
    s_led.mutex = NULL;

    // Deinit LED driver
    led_driver_deinit();

    ESP_LOGI(TAG, "Deinitialized");
    return ESP_OK;
}

status_led_state_t status_led_get_state(void)
{
    if (!s_led.initialized) {
        return STATUS_LED_OFF;
    }

    // Return the currently displayed state
    return s_led.displayed_state;
}

// --------------------------------------------------------------------------
// Config API Implementation
// --------------------------------------------------------------------------

esp_err_t status_led_set_enabled(bool enabled)
{
    if (!s_led.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_led.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_led.enabled = enabled;
    ESP_LOGI(TAG, "Status LED %s", enabled ? "enabled" : "disabled");

    esp_err_t ret = ESP_OK;
    if (!enabled) {
        // Stop all patterns when disabled
        for (int i = 0; i < STATUS_LED_MAX; i++) {
            led_driver_stop_pattern(i);
        }
    } else {
        // Re-apply current state when re-enabled
        ret = apply_pattern(s_led.displayed_state);
    }

    xSemaphoreGive(s_led.mutex);
    return ret;
}

esp_err_t status_led_set_brightness(uint8_t percent)
{
    if (percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_led.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_led.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_led.brightness = percent;
    ESP_LOGI(TAG, "Brightness set to %d%%", percent);

    esp_err_t ret = led_driver_set_brightness(percent);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set brightness: %s", esp_err_to_name(ret));
    }

    xSemaphoreGive(s_led.mutex);
    return ret;
}

esp_err_t status_led_set_alerts_only(bool alerts_only)
{
    if (!s_led.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_led.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_led.alerts_only = alerts_only;
    ESP_LOGI(TAG, "Alerts-only mode %s", alerts_only ? "enabled" : "disabled");
    esp_err_t ret = apply_pattern(s_led.displayed_state);  // Re-evaluate what should be shown

    xSemaphoreGive(s_led.mutex);
    return ret;
}

// Reading a single bool is atomic on ESP32, no mutex needed
bool status_led_is_enabled(void)
{
    return s_led.enabled;
}

// Reading a single uint8_t is atomic on ESP32, no mutex needed
uint8_t status_led_get_brightness(void)
{
    return s_led.brightness;
}

// Reading a single bool is atomic on ESP32, no mutex needed
bool status_led_is_alerts_only(void)
{
    return s_led.alerts_only;
}

// --------------------------------------------------------------------------
// Utility Functions
// --------------------------------------------------------------------------

const char *status_led_state_name(status_led_state_t state)
{
    if (state >= STATUS_LED_MAX) {
        return "UNKNOWN";
    }
    return state_names[state];
}

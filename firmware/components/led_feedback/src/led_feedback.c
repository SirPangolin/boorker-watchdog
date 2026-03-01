/**
 * @file led_feedback.c
 * @brief LED status feedback core implementation
 *
 * Provides visual status indication via onboard LED using the led_indicator
 * component. Supports priority-based state management with thread-safe access.
 */

#include "led_feedback.h"
#include "led_indicator.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include <string.h>

static const char *TAG = "led_feedback";

// NVS configuration
#define NVS_NAMESPACE "led_cfg"
#define NVS_KEY_ENABLED "enabled"
#define NVS_KEY_BRIGHTNESS "brightness"
#define NVS_KEY_ALERTS_ONLY "alerts_only"

// Mutex timeout
#define MUTEX_TIMEOUT_MS 100

// Handle CONFIG_LED_FEEDBACK_ACTIVE_LOW - defaults to false if not defined
#ifndef CONFIG_LED_FEEDBACK_ACTIVE_LOW
#define CONFIG_LED_FEEDBACK_ACTIVE_LOW 0
#endif

// External pattern array from led_patterns.c
extern blink_step_t const *led_patterns[];

// State names for logging
static const char *state_names[] = {
    [LED_FB_ALERT_CRITICAL]       = "ALERT_CRITICAL",
    [LED_FB_FIRST_BOOT]           = "FIRST_BOOT",
    [LED_FB_WIFI_PROVISIONING]    = "WIFI_PROVISIONING",
    [LED_FB_WIFI_RECONNECTING]    = "WIFI_RECONNECTING",
    [LED_FB_ALERT_ACTIVE]         = "ALERT_ACTIVE",
    [LED_FB_WIFI_CONNECTING]      = "WIFI_CONNECTING",
    [LED_FB_TAILSCALE_CONNECTING] = "TAILSCALE_CONNECTING",
    [LED_FB_CONNECTED]            = "CONNECTED",
    [LED_FB_OFF]                  = "OFF",
};

// Verify state_names array matches enum
_Static_assert(sizeof(state_names) / sizeof(state_names[0]) == LED_FB_MAX,
               "state_names array size must match LED_FB_MAX");

// Static state structure
static struct {
    bool initialized;
    bool enabled;
    uint8_t brightness;
    bool alerts_only;
    led_state_t current_state;
    led_indicator_handle_t handle;
    SemaphoreHandle_t mutex;
} s_led = {
    .initialized = false,
    .enabled = true,
    .brightness = CONFIG_LED_FEEDBACK_DEFAULT_BRIGHTNESS,
    .alerts_only = false,
    .current_state = LED_FB_OFF,
    .handle = NULL,
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
    }

    // Load brightness
    uint8_t brightness_val;
    ret = nvs_get_u8(handle, NVS_KEY_BRIGHTNESS, &brightness_val);
    if (ret == ESP_OK) {
        s_led.brightness = (brightness_val <= 100) ? brightness_val : 100;
    }

    // Load alerts_only
    uint8_t alerts_only_val;
    ret = nvs_get_u8(handle, NVS_KEY_ALERTS_ONLY, &alerts_only_val);
    if (ret == ESP_OK) {
        s_led.alerts_only = (alerts_only_val != 0);
    }

    nvs_close(handle);

    ESP_LOGI(TAG, "Loaded config: enabled=%d, brightness=%d, alerts_only=%d",
             s_led.enabled, s_led.brightness, s_led.alerts_only);
}

esp_err_t led_feedback_save_config(void)
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
static bool should_show_state(led_state_t state)
{
    // If disabled, never show anything
    if (!s_led.enabled) {
        return false;
    }

    // If alerts_only mode, only show alert states
    if (s_led.alerts_only) {
        return (state == LED_FB_ALERT_CRITICAL || state == LED_FB_ALERT_ACTIVE);
    }

    // Show all states when not in alerts_only mode
    return true;
}

/**
 * @brief Apply the current state to the LED
 *
 * Must be called with mutex held.
 */
static void apply_current_state(void)
{
    if (s_led.handle == NULL) {
        return;
    }

    // Stop any current pattern
    led_indicator_stop(s_led.handle, (int)s_led.current_state);

    if (should_show_state(s_led.current_state)) {
        // Start the pattern for current state
        led_indicator_start(s_led.handle, (int)s_led.current_state);
        ESP_LOGD(TAG, "Showing state: %s", state_names[s_led.current_state]);
    } else {
        // Turn off LED
        led_indicator_start(s_led.handle, (int)LED_FB_OFF);
        ESP_LOGD(TAG, "State %s suppressed", state_names[s_led.current_state]);
    }
}

// --------------------------------------------------------------------------
// Core API Implementation
// --------------------------------------------------------------------------

esp_err_t led_feedback_init(void)
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

    // Configure LED indicator with LEDC (PWM) mode
    led_indicator_ledc_config_t ledc_config = {
        .is_active_level_high = !CONFIG_LED_FEEDBACK_ACTIVE_LOW,
        .timer_inited = false,
        .timer_num = LEDC_TIMER_0,
        .gpio_num = CONFIG_LED_FEEDBACK_GPIO,
        .channel = LEDC_CHANNEL_0,
    };

    led_indicator_config_t config = {
        .mode = LED_LEDC_MODE,
        .led_indicator_ledc_config = &ledc_config,
        .blink_lists = led_patterns,
        .blink_list_num = LED_FB_MAX,
    };

    s_led.handle = led_indicator_create(&config);
    if (s_led.handle == NULL) {
        ESP_LOGE(TAG, "Failed to create LED indicator");
        vSemaphoreDelete(s_led.mutex);
        s_led.mutex = NULL;
        return ESP_FAIL;
    }

    // Set initial brightness
    led_indicator_set_brightness(s_led.handle, s_led.brightness);

    s_led.initialized = true;
    s_led.current_state = LED_FB_OFF;

    ESP_LOGI(TAG, "Initialized on GPIO %d (brightness=%d%%)",
             CONFIG_LED_FEEDBACK_GPIO, s_led.brightness);

    return ESP_OK;
}

esp_err_t led_feedback_deinit(void)
{
    if (!s_led.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Take mutex before cleanup
    if (xSemaphoreTake(s_led.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout during deinit, proceeding anyway");
    }

    // Stop any active pattern
    if (s_led.handle != NULL) {
        led_indicator_stop(s_led.handle, (int)s_led.current_state);
        led_indicator_delete(s_led.handle);
        s_led.handle = NULL;
    }

    s_led.initialized = false;
    s_led.current_state = LED_FB_OFF;

    // Give back mutex before deleting
    xSemaphoreGive(s_led.mutex);
    vSemaphoreDelete(s_led.mutex);
    s_led.mutex = NULL;

    ESP_LOGI(TAG, "Deinitialized");
    return ESP_OK;
}

esp_err_t led_feedback_set_state(led_state_t state)
{
    if (state >= LED_FB_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_led.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_led.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in set_state");
        return ESP_ERR_TIMEOUT;
    }

    // Lower enum value = higher priority
    // Only change if new state has higher or equal priority
    if (state <= s_led.current_state || s_led.current_state == LED_FB_OFF) {
        if (s_led.current_state != state) {
            // Stop old pattern
            if (s_led.handle != NULL) {
                led_indicator_stop(s_led.handle, (int)s_led.current_state);
            }

            s_led.current_state = state;
            ESP_LOGI(TAG, "State -> %s", state_names[state]);

            apply_current_state();
        }
    } else {
        ESP_LOGD(TAG, "State %s ignored (current %s has higher priority)",
                 state_names[state], state_names[s_led.current_state]);
    }

    xSemaphoreGive(s_led.mutex);
    return ESP_OK;
}

esp_err_t led_feedback_clear_state(led_state_t state)
{
    if (state >= LED_FB_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_led.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_led.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in clear_state");
        return ESP_ERR_TIMEOUT;
    }

    if (s_led.current_state == state) {
        // Stop current pattern
        if (s_led.handle != NULL) {
            led_indicator_stop(s_led.handle, (int)state);
        }

        // Fall back to OFF state
        // Note: In a full implementation, you might track multiple active states
        // and fall back to the next highest priority. For simplicity, we go to OFF.
        s_led.current_state = LED_FB_OFF;
        ESP_LOGI(TAG, "Cleared %s -> OFF", state_names[state]);

        apply_current_state();
    }

    xSemaphoreGive(s_led.mutex);
    return ESP_OK;
}

led_state_t led_feedback_get_state(void)
{
    if (!s_led.initialized) {
        return LED_FB_OFF;
    }

    // Reading a single enum value is atomic on ESP32
    return s_led.current_state;
}

// --------------------------------------------------------------------------
// Config API Implementation
// --------------------------------------------------------------------------

esp_err_t led_feedback_set_enabled(bool enabled)
{
    if (!s_led.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_led.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_led.enabled = enabled;
    ESP_LOGI(TAG, "LED feedback %s", enabled ? "enabled" : "disabled");

    if (!enabled) {
        // Turn off LED when disabled
        for (int i = 0; i < LED_FB_MAX; i++) {
            led_indicator_stop(s_led.handle, i);
        }
    } else {
        // Re-apply current state when re-enabled
        apply_current_state();
    }

    xSemaphoreGive(s_led.mutex);
    return ESP_OK;
}

esp_err_t led_feedback_set_brightness(uint8_t percent)
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

    if (s_led.handle != NULL) {
        led_indicator_set_brightness(s_led.handle, percent);
    }

    xSemaphoreGive(s_led.mutex);
    return ESP_OK;
}

esp_err_t led_feedback_set_alerts_only(bool alerts_only)
{
    if (!s_led.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_led.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_led.alerts_only = alerts_only;
    ESP_LOGI(TAG, "Alerts-only mode %s", alerts_only ? "enabled" : "disabled");
    apply_current_state();  // Re-evaluate what should be shown

    xSemaphoreGive(s_led.mutex);
    return ESP_OK;
}

bool led_feedback_is_enabled(void)
{
    return s_led.enabled;
}

uint8_t led_feedback_get_brightness(void)
{
    return s_led.brightness;
}

bool led_feedback_is_alerts_only(void)
{
    return s_led.alerts_only;
}

// --------------------------------------------------------------------------
// Utility Functions
// --------------------------------------------------------------------------

const char *led_feedback_state_name(led_state_t state)
{
    if (state >= LED_FB_MAX) {
        return "UNKNOWN";
    }
    return state_names[state];
}

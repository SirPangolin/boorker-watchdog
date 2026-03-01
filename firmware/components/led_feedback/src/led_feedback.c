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

#if CONFIG_LED_FEEDBACK_TYPE_WS2812
#include "led_strips.h"
#include "driver/rmt_types.h"
#endif

#include <string.h>

static const char *TAG = "led_feedback";

// NVS configuration
#define NVS_NAMESPACE "led_cfg"
#define NVS_KEY_ENABLED "enabled"
#define NVS_KEY_BRIGHTNESS "brightness"
#define NVS_KEY_ALERTS_ONLY "alerts_only"

// Mutex timeout
#define MUTEX_TIMEOUT_MS 100
#define DEINIT_MUTEX_TIMEOUT_MS 500  // Longer timeout for deinit (5x normal)

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
    uint16_t active_states;      // Bitmask of active states
    led_state_t displayed_state; // Currently displayed state (for stopping pattern)
    led_indicator_handle_t handle;
    SemaphoreHandle_t mutex;
} s_led = {
    .initialized = false,
    .enabled = true,
    .brightness = CONFIG_LED_FEEDBACK_DEFAULT_BRIGHTNESS,
    .alerts_only = false,
    .active_states = 0,
    .displayed_state = LED_FB_OFF,
    .handle = NULL,
    .mutex = NULL,
};

/**
 * @brief Get highest priority active state (lowest bit set)
 * @return Highest priority active state, or LED_FB_OFF if none active
 */
static led_state_t get_highest_priority_state(void)
{
    if (s_led.active_states == 0) {
        return LED_FB_OFF;
    }
    // Find lowest set bit (highest priority state)
    for (int i = 0; i < LED_FB_MAX; i++) {
        if (s_led.active_states & (1 << i)) {
            return (led_state_t)i;
        }
    }
    return LED_FB_OFF;
}

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
 * @brief Apply the highest priority active state to the LED
 *
 * Must be called with mutex held. Determines which state should be displayed
 * based on active_states bitmask and updates the LED accordingly.
 *
 * @return ESP_OK on success, or error from led_indicator functions
 */
static esp_err_t apply_current_state(void)
{
    if (s_led.handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;
    led_state_t new_state = get_highest_priority_state();

    // Skip if no change needed
    if (new_state == s_led.displayed_state) {
        return ESP_OK;
    }

    // Stop currently displayed pattern
    ret = led_indicator_stop(s_led.handle, (int)s_led.displayed_state);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop pattern %s: %s",
                 state_names[s_led.displayed_state], esp_err_to_name(ret));
    }

    // Determine what to show
    led_state_t show_state = new_state;
    if (!should_show_state(new_state)) {
        show_state = LED_FB_OFF;
    }

    // Start new pattern
    ret = led_indicator_start(s_led.handle, (int)show_state);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start pattern %s: %s",
                 state_names[show_state], esp_err_to_name(ret));
        return ret;
    }

    s_led.displayed_state = new_state;
    ESP_LOGD(TAG, "Displaying: %s", state_names[show_state]);

    return ESP_OK;
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

#if CONFIG_LED_FEEDBACK_TYPE_WS2812
    // Configure LED indicator with WS2812 RGB strip mode
    led_indicator_strips_config_t strips_config = {
        .led_strip_cfg = {
            .strip_gpio_num = CONFIG_LED_FEEDBACK_GPIO,
            .max_leds = 1,  // Single onboard LED
            .led_model = LED_MODEL_WS2812,
            .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
            .flags.invert_out = CONFIG_LED_FEEDBACK_ACTIVE_LOW,
        },
        .led_strip_driver = LED_STRIP_RMT,
        .led_strip_rmt_cfg = {
            .clk_src = RMT_CLK_SRC_DEFAULT,
            .resolution_hz = 10000000,  // 10 MHz
            .mem_block_symbols = 0,     // Use default
            .flags.with_dma = true,
        },
    };

    led_indicator_config_t config = {
        .mode = LED_STRIPS_MODE,
        .led_indicator_strips_config = &strips_config,
        .blink_lists = led_patterns,
        .blink_list_num = LED_FB_MAX,
    };
#else
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
#endif

    s_led.handle = led_indicator_create(&config);
    if (s_led.handle == NULL) {
        ESP_LOGE(TAG, "Failed to create LED indicator");
        vSemaphoreDelete(s_led.mutex);
        s_led.mutex = NULL;
        return ESP_FAIL;
    }

    // Set initial brightness
    esp_err_t brightness_ret = led_indicator_set_brightness(s_led.handle, s_led.brightness);
    if (brightness_ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set initial brightness: %s", esp_err_to_name(brightness_ret));
    }

    s_led.initialized = true;
    s_led.active_states = 0;
    s_led.displayed_state = LED_FB_OFF;

#if CONFIG_LED_FEEDBACK_TYPE_WS2812
    ESP_LOGI(TAG, "Initialized WS2812 on GPIO %d (brightness=%d%%)",
             CONFIG_LED_FEEDBACK_GPIO, s_led.brightness);
#else
    ESP_LOGI(TAG, "Initialized LEDC on GPIO %d (brightness=%d%%)",
             CONFIG_LED_FEEDBACK_GPIO, s_led.brightness);
#endif

    return ESP_OK;
}

esp_err_t led_feedback_deinit(void)
{
    if (!s_led.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Take mutex before cleanup with longer timeout for deinit
    if (xSemaphoreTake(s_led.mutex, pdMS_TO_TICKS(DEINIT_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Mutex timeout during deinit after %d ms", DEINIT_MUTEX_TIMEOUT_MS);
        return ESP_ERR_TIMEOUT;
    }

    // Stop any active pattern
    if (s_led.handle != NULL) {
        esp_err_t ret = led_indicator_stop(s_led.handle, (int)s_led.displayed_state);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop pattern during deinit: %s", esp_err_to_name(ret));
        }
        led_indicator_delete(s_led.handle);
        s_led.handle = NULL;
    }

    s_led.initialized = false;
    s_led.active_states = 0;
    s_led.displayed_state = LED_FB_OFF;

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

    // Set bit in active states bitmask
    uint16_t old_states = s_led.active_states;
    s_led.active_states |= (1 << state);

    esp_err_t ret = ESP_OK;
    if (s_led.active_states != old_states) {
        ESP_LOGI(TAG, "State + %s (active: 0x%04x)", state_names[state], s_led.active_states);
        ret = apply_current_state();
    }

    xSemaphoreGive(s_led.mutex);
    return ret;
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

    // Clear bit in active states bitmask
    uint16_t old_states = s_led.active_states;
    s_led.active_states &= ~(1 << state);

    esp_err_t ret = ESP_OK;
    if (s_led.active_states != old_states) {
        ESP_LOGI(TAG, "State - %s (active: 0x%04x)", state_names[state], s_led.active_states);
        ret = apply_current_state();  // Will show next highest priority
    }

    xSemaphoreGive(s_led.mutex);
    return ret;
}

led_state_t led_feedback_get_state(void)
{
    if (!s_led.initialized) {
        return LED_FB_OFF;
    }

    // Return the currently displayed state
    return s_led.displayed_state;
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

    esp_err_t ret = ESP_OK;
    if (!enabled) {
        // Turn off LED when disabled
        for (int i = 0; i < LED_FB_MAX; i++) {
            esp_err_t stop_ret = led_indicator_stop(s_led.handle, i);
            if (stop_ret != ESP_OK) {
                ESP_LOGW(TAG, "Failed to stop pattern %d: %s", i, esp_err_to_name(stop_ret));
                ret = stop_ret;  // Report last error
            }
        }
    } else {
        // Re-apply current state when re-enabled
        ret = apply_current_state();
    }

    xSemaphoreGive(s_led.mutex);
    return ret;
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

    esp_err_t ret = ESP_OK;
    if (s_led.handle != NULL) {
        ret = led_indicator_set_brightness(s_led.handle, percent);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set brightness: %s", esp_err_to_name(ret));
        }
    }

    xSemaphoreGive(s_led.mutex);
    return ret;
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
    esp_err_t ret = apply_current_state();  // Re-evaluate what should be shown

    xSemaphoreGive(s_led.mutex);
    return ret;
}

// Reading a single bool is atomic on ESP32, no mutex needed
bool led_feedback_is_enabled(void)
{
    return s_led.enabled;
}

// Reading a single uint8_t is atomic on ESP32, no mutex needed
uint8_t led_feedback_get_brightness(void)
{
    return s_led.brightness;
}

// Reading a single bool is atomic on ESP32, no mutex needed
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

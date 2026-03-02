/**
 * @file led_feedback.c
 * @brief LED status feedback core implementation (ANDON channel subscriber)
 *
 * Provides visual status indication via onboard LED (and optional external LED)
 * using the led_indicator component. Subscribes to andon_service for state
 * notifications and renders appropriate LED patterns.
 */

#include "led_feedback.h"
#include "andon_service.h"
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

#if CONFIG_LED_FEEDBACK_EXTERNAL_ENABLED && CONFIG_LED_FEEDBACK_EXTERNAL_TYPE_WS2812
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
    led_state_t displayed_state; // Currently displayed state (for stopping pattern)
    led_indicator_handle_t handle;
#if CONFIG_LED_FEEDBACK_EXTERNAL_ENABLED
    led_indicator_handle_t external_handle;
#endif
    SemaphoreHandle_t mutex;
} s_led = {
    .initialized = false,
    .enabled = true,
    .brightness = CONFIG_LED_FEEDBACK_DEFAULT_BRIGHTNESS,
    .alerts_only = false,
    .displayed_state = LED_FB_OFF,
    .handle = NULL,
#if CONFIG_LED_FEEDBACK_EXTERNAL_ENABLED
    .external_handle = NULL,
#endif
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
 * @brief Apply a pattern to the LED(s)
 *
 * Must be called with mutex held. Updates the LED indicator(s) to display
 * the specified pattern.
 *
 * @param state The LED pattern state to apply
 * @return ESP_OK on success, or error from led_indicator functions
 */
static esp_err_t apply_pattern(led_state_t state)
{
    if (s_led.handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ESP_OK;

    // Skip if no change needed
    if (state == s_led.displayed_state) {
        return ESP_OK;
    }

    // Stop currently displayed pattern on primary LED
    ret = led_indicator_stop(s_led.handle, (int)s_led.displayed_state);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop pattern %s: %s",
                 state_names[s_led.displayed_state], esp_err_to_name(ret));
    }

#if CONFIG_LED_FEEDBACK_EXTERNAL_ENABLED
    // Stop currently displayed pattern on external LED
    if (s_led.external_handle != NULL) {
        esp_err_t ext_ret = led_indicator_stop(s_led.external_handle, (int)s_led.displayed_state);
        if (ext_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop external pattern %s: %s",
                     state_names[s_led.displayed_state], esp_err_to_name(ext_ret));
        }
    }
#endif

    // Determine what to show
    led_state_t show_state = state;
    if (!should_show_state(state)) {
        show_state = LED_FB_OFF;
    }

    // Start new pattern on primary LED
    ret = led_indicator_start(s_led.handle, (int)show_state);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to start pattern %s: %s",
                 state_names[show_state], esp_err_to_name(ret));
        return ret;
    }

#if CONFIG_LED_FEEDBACK_EXTERNAL_ENABLED
    // Start new pattern on external LED
    if (s_led.external_handle != NULL) {
        esp_err_t ext_ret = led_indicator_start(s_led.external_handle, (int)show_state);
        if (ext_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start external pattern %s: %s",
                     state_names[show_state], esp_err_to_name(ext_ret));
        }
    }
#endif

    s_led.displayed_state = state;
    ESP_LOGD(TAG, "Displaying: %s", state_names[show_state]);

    return ESP_OK;
}

/**
 * @brief Map ANDON state to internal LED pattern
 *
 * @param andon_state ANDON state from callback
 * @return Corresponding led_state_t pattern
 */
static led_state_t map_andon_to_led_state(andon_state_t andon_state)
{
    switch (andon_state) {
        case ANDON_FIRST_BOOT:           return LED_FB_FIRST_BOOT;
        case ANDON_ERROR:                return LED_FB_ALERT_CRITICAL;
        case ANDON_WIFI_PROVISIONING:    return LED_FB_WIFI_PROVISIONING;
        case ANDON_WIFI_RECONNECTING:    return LED_FB_WIFI_RECONNECTING;
        case ANDON_WIFI_CONNECTING:      return LED_FB_WIFI_CONNECTING;
        case ANDON_TAILSCALE_CONNECTING: return LED_FB_TAILSCALE_CONNECTING;
        case ANDON_CONNECTED:            return LED_FB_CONNECTED;
        case ANDON_OFF:                  return LED_FB_OFF;
        case ANDON_ALERT_CRITICAL:       return LED_FB_ALERT_CRITICAL;
        case ANDON_ALERT_ACTIVE:         return LED_FB_ALERT_ACTIVE;
        case ANDON_SENSOR_WARNING:       return LED_FB_ALERT_ACTIVE;  // Use same pattern
        default:                         return LED_FB_OFF;
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
    led_state_t led_state = map_andon_to_led_state(state);

    ESP_LOGI(TAG, "ANDON state: %s -> LED pattern: %s",
             andon_state_name(state), state_names[led_state]);

    // Apply the pattern
    apply_pattern(led_state);

    xSemaphoreGive(s_led.mutex);
}

// --------------------------------------------------------------------------
// External LED Initialization
// --------------------------------------------------------------------------

#if CONFIG_LED_FEEDBACK_EXTERNAL_ENABLED
/**
 * @brief Initialize external LED indicator
 *
 * @return ESP_OK on success, or error from led_indicator_create
 */
static esp_err_t init_external_led(void)
{
#if CONFIG_LED_FEEDBACK_EXTERNAL_TYPE_WS2812
    // Configure external LED with WS2812 RGB strip mode
    led_indicator_strips_config_t strips_config = {
        .led_strip_cfg = {
            .strip_gpio_num = CONFIG_LED_FEEDBACK_EXTERNAL_GPIO_DATA,
            .max_leds = 1,
            .led_model = LED_MODEL_WS2812,
            .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
            .flags.invert_out = false,
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

#elif CONFIG_LED_FEEDBACK_EXTERNAL_TYPE_LEDC
    // Configure external LED with LEDC (PWM) mode - use channel 1 to avoid conflict
    led_indicator_ledc_config_t ledc_config = {
        .is_active_level_high = true,
        .timer_inited = true,  // Primary LED already initialized timer
        .timer_num = LEDC_TIMER_0,
        .gpio_num = CONFIG_LED_FEEDBACK_EXTERNAL_GPIO_DATA,
        .channel = LEDC_CHANNEL_1,
    };

    led_indicator_config_t config = {
        .mode = LED_LEDC_MODE,
        .led_indicator_ledc_config = &ledc_config,
        .blink_lists = led_patterns,
        .blink_list_num = LED_FB_MAX,
    };

#elif CONFIG_LED_FEEDBACK_EXTERNAL_TYPE_RGB_LEDC
    // Configure external LED with RGB LEDC (3-channel PWM)
    // Note: This requires led_indicator RGB LEDC support
    led_indicator_ledc_config_t ledc_config_r = {
        .is_active_level_high = true,
        .timer_inited = true,
        .timer_num = LEDC_TIMER_0,
        .gpio_num = CONFIG_LED_FEEDBACK_EXTERNAL_GPIO_DATA,
        .channel = LEDC_CHANNEL_1,
    };

    // For RGB_LEDC, we use single LEDC channel for simplicity
    // Full RGB would require led_indicator RGB mode support
    led_indicator_config_t config = {
        .mode = LED_LEDC_MODE,
        .led_indicator_ledc_config = &ledc_config_r,
        .blink_lists = led_patterns,
        .blink_list_num = LED_FB_MAX,
    };
    ESP_LOGW(TAG, "RGB_LEDC uses single channel only (full RGB not yet implemented)");

#elif CONFIG_LED_FEEDBACK_EXTERNAL_TYPE_GPIO
    // Configure external LED with simple GPIO mode
    led_indicator_gpio_config_t gpio_config = {
        .is_active_level_high = true,
        .gpio_num = CONFIG_LED_FEEDBACK_EXTERNAL_GPIO_DATA,
    };

    led_indicator_config_t config = {
        .mode = LED_GPIO_MODE,
        .led_indicator_gpio_config = &gpio_config,
        .blink_lists = led_patterns,
        .blink_list_num = LED_FB_MAX,
    };
#endif

    s_led.external_handle = led_indicator_create(&config);
    if (s_led.external_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create external LED indicator");
        return ESP_FAIL;
    }

    // Set brightness on external LED
    esp_err_t ret = led_indicator_set_brightness(s_led.external_handle, s_led.brightness);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set external LED brightness: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "External LED initialized on GPIO %d", CONFIG_LED_FEEDBACK_EXTERNAL_GPIO_DATA);
    return ESP_OK;
}
#endif

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

#if CONFIG_LED_FEEDBACK_EXTERNAL_ENABLED
    // Initialize external LED (non-fatal if it fails)
    esp_err_t ext_ret = init_external_led();
    if (ext_ret != ESP_OK) {
        ESP_LOGW(TAG, "External LED initialization failed, continuing without it");
    }
#endif

    s_led.initialized = true;
    s_led.displayed_state = LED_FB_OFF;

#if CONFIG_LED_FEEDBACK_TYPE_WS2812
    ESP_LOGI(TAG, "Initialized WS2812 on GPIO %d (brightness=%d%%)",
             CONFIG_LED_FEEDBACK_GPIO, s_led.brightness);
#else
    ESP_LOGI(TAG, "Initialized LEDC on GPIO %d (brightness=%d%%)",
             CONFIG_LED_FEEDBACK_GPIO, s_led.brightness);
#endif

    // Register as ANDON channel
    esp_err_t ret = andon_register_channel("led", andon_callback, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ANDON channel: %s - LED will NOT show system status!",
                 esp_err_to_name(ret));
        // Non-fatal for init, but LED won't respond to WiFi/error/alert states
    } else {
        ESP_LOGI(TAG, "Registered as ANDON channel");
    }

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

    // Stop any active pattern on primary LED
    if (s_led.handle != NULL) {
        esp_err_t ret = led_indicator_stop(s_led.handle, (int)s_led.displayed_state);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop pattern during deinit: %s", esp_err_to_name(ret));
        }
        led_indicator_delete(s_led.handle);
        s_led.handle = NULL;
    }

#if CONFIG_LED_FEEDBACK_EXTERNAL_ENABLED
    // Stop any active pattern on external LED
    if (s_led.external_handle != NULL) {
        esp_err_t ret = led_indicator_stop(s_led.external_handle, (int)s_led.displayed_state);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop external pattern during deinit: %s", esp_err_to_name(ret));
        }
        led_indicator_delete(s_led.external_handle);
        s_led.external_handle = NULL;
    }
#endif

    s_led.initialized = false;
    s_led.displayed_state = LED_FB_OFF;

    // Give back mutex before deleting
    xSemaphoreGive(s_led.mutex);
    vSemaphoreDelete(s_led.mutex);
    s_led.mutex = NULL;

    ESP_LOGI(TAG, "Deinitialized");
    return ESP_OK;
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
#if CONFIG_LED_FEEDBACK_EXTERNAL_ENABLED
            if (s_led.external_handle != NULL) {
                led_indicator_stop(s_led.external_handle, i);
            }
#endif
        }
    } else {
        // Re-apply current state when re-enabled
        ret = apply_pattern(s_led.displayed_state);
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

#if CONFIG_LED_FEEDBACK_EXTERNAL_ENABLED
    if (s_led.external_handle != NULL) {
        esp_err_t ext_ret = led_indicator_set_brightness(s_led.external_handle, percent);
        if (ext_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set external brightness: %s", esp_err_to_name(ext_ret));
        }
    }
#endif

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
    esp_err_t ret = apply_pattern(s_led.displayed_state);  // Re-evaluate what should be shown

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

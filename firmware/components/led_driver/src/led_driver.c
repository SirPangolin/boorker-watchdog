/**
 * @file led_driver.c
 * @brief LED hardware abstraction layer implementation
 *
 * Uses esp-idf-lib led_indicator for WS2812 addressable LEDs.
 * Thread-safe with mutex protection.
 */

#include "led_driver.h"
#include "led_indicator.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include "driver/ledc.h"  // For LEDC_TIMER_*, LEDC_CHANNEL_*

static const char *TAG = "led_driver";

/**
 * @brief Convert RGB to perceived luminance (0-255)
 * Standard luminance formula: 0.299R + 0.587G + 0.114B
 * Integer math: (77*R + 150*G + 29*B) / 256
 */
static inline uint8_t rgb_to_luminance(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint8_t)((77 * r + 150 * g + 29 * b) >> 8);
}

/**
 * @brief Internal driver context
 */
typedef struct {
    led_indicator_handle_t onboard_handle;   /**< Handle for onboard LED */
    led_indicator_handle_t external_handle;  /**< Handle for external LED (future) */
    led_driver_caps_t caps;                  /**< Cached capabilities */
    uint8_t brightness;                      /**< Current brightness (0-100) */
    SemaphoreHandle_t mutex;                 /**< Mutex for thread safety */
    bool initialized;                        /**< Initialization state */
    blink_step_t const **patterns;           /**< Registered blink patterns */
    size_t num_patterns;                     /**< Number of patterns */
    bool patterns_registered;                /**< Whether patterns have been registered */
} led_driver_ctx_t;

static led_driver_ctx_t s_ctx = {0};

/**
 * @brief Recreate LED indicator handles with pattern support
 *
 * Called by led_driver_register_patterns() to rebuild indicators with blink lists.
 */
static esp_err_t recreate_indicators_with_patterns(void)
{
    // Delete existing handles
    if (s_ctx.onboard_handle) {
        led_indicator_delete(s_ctx.onboard_handle);
        s_ctx.onboard_handle = NULL;
    }
    if (s_ctx.external_handle) {
        led_indicator_delete(s_ctx.external_handle);
        s_ctx.external_handle = NULL;
    }

    // Recreate onboard LED with patterns
#if CONFIG_LED_DRIVER_ONBOARD_WS2812
    led_indicator_strips_config_t strips_config = {
        .led_strip_cfg = {
            .strip_gpio_num = CONFIG_LED_DRIVER_ONBOARD_GPIO_NUM,
            .max_leds = 1,
            .led_model = LED_MODEL_WS2812,
            .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
            .flags = { .invert_out = false },
        },
        .led_strip_driver = LED_STRIP_RMT,
    };

    led_indicator_config_t config = {
        .mode = LED_STRIPS_MODE,
        .led_indicator_strips_config = &strips_config,
        .blink_lists = s_ctx.patterns,
        .blink_list_num = s_ctx.num_patterns,
    };

    s_ctx.onboard_handle = led_indicator_create(&config);
    if (!s_ctx.onboard_handle) {
        ESP_LOGE(TAG, "Failed to recreate onboard WS2812 with patterns");
        return ESP_FAIL;
    }
#elif CONFIG_LED_DRIVER_ONBOARD_GPIO
    led_indicator_gpio_config_t gpio_config = {
        .gpio_num = CONFIG_LED_DRIVER_ONBOARD_GPIO_NUM,
        .is_active_level_high = true,
    };

    led_indicator_config_t config = {
        .mode = LED_GPIO_MODE,
        .led_indicator_gpio_config = &gpio_config,
        .blink_lists = s_ctx.patterns,
        .blink_list_num = s_ctx.num_patterns,
    };

    s_ctx.onboard_handle = led_indicator_create(&config);
    if (!s_ctx.onboard_handle) {
        ESP_LOGE(TAG, "Failed to recreate onboard GPIO with patterns");
        return ESP_FAIL;
    }
#endif

    // Recreate external LED with patterns
#if CONFIG_LED_DRIVER_EXTERNAL_ENABLED

#if CONFIG_LED_DRIVER_EXTERNAL_RGB_LEDC
    led_indicator_rgb_config_t rgb_config = {
        .is_active_level_high = true,
        .timer_inited = false,
        .timer_num = LEDC_TIMER_1,
        .red_gpio_num = CONFIG_LED_DRIVER_EXTERNAL_GPIO_R,
        .green_gpio_num = CONFIG_LED_DRIVER_EXTERNAL_GPIO_G,
        .blue_gpio_num = CONFIG_LED_DRIVER_EXTERNAL_GPIO_B,
        .red_channel = LEDC_CHANNEL_1,
        .green_channel = LEDC_CHANNEL_2,
        .blue_channel = LEDC_CHANNEL_3,
    };

    led_indicator_config_t ext_config = {
        .mode = LED_RGB_MODE,
        .led_indicator_rgb_config = &rgb_config,
        .blink_lists = s_ctx.patterns,
        .blink_list_num = s_ctx.num_patterns,
    };

    s_ctx.external_handle = led_indicator_create(&ext_config);
    if (!s_ctx.external_handle) {
        ESP_LOGW(TAG, "Failed to recreate external RGB LEDC with patterns");
    }

#elif CONFIG_LED_DRIVER_EXTERNAL_MONO_LEDC
    led_indicator_ledc_config_t ledc_config = {
        .is_active_level_high = true,
        .timer_inited = false,
        .timer_num = LEDC_TIMER_1,
        .gpio_num = CONFIG_LED_DRIVER_EXTERNAL_GPIO_R,
        .channel = LEDC_CHANNEL_1,
    };

    led_indicator_config_t ext_config = {
        .mode = LED_LEDC_MODE,
        .led_indicator_ledc_config = &ledc_config,
        .blink_lists = s_ctx.patterns,
        .blink_list_num = s_ctx.num_patterns,
    };

    s_ctx.external_handle = led_indicator_create(&ext_config);
    if (!s_ctx.external_handle) {
        ESP_LOGW(TAG, "Failed to recreate external MONO LEDC with patterns");
    }

#elif CONFIG_LED_DRIVER_EXTERNAL_GPIO
    led_indicator_gpio_config_t ext_gpio_config = {
        .is_active_level_high = true,
        .gpio_num = CONFIG_LED_DRIVER_EXTERNAL_GPIO_R,
    };

    led_indicator_config_t ext_config = {
        .mode = LED_GPIO_MODE,
        .led_indicator_gpio_config = &ext_gpio_config,
        .blink_lists = s_ctx.patterns,
        .blink_list_num = s_ctx.num_patterns,
    };

    s_ctx.external_handle = led_indicator_create(&ext_config);
    if (!s_ctx.external_handle) {
        ESP_LOGW(TAG, "Failed to recreate external GPIO with patterns");
    }
#endif

#endif // CONFIG_LED_DRIVER_EXTERNAL_ENABLED

    ESP_LOGI(TAG, "LED indicators recreated with %zu patterns", s_ctx.num_patterns);
    return ESP_OK;
}

esp_err_t led_driver_init(void)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing LED driver");

    // Create mutex for thread safety
    s_ctx.mutex = xSemaphoreCreateMutex();
    if (s_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initialize pattern state
    s_ctx.patterns = NULL;
    s_ctx.num_patterns = 0;
    s_ctx.patterns_registered = false;

    // Set default brightness
    s_ctx.brightness = 100;

    // Initialize capabilities based on configuration
    s_ctx.caps.supports_rgb = false;
    s_ctx.caps.supports_brightness = false;

#if CONFIG_LED_DRIVER_ONBOARD_WS2812
    ESP_LOGI(TAG, "Configuring onboard WS2812 on GPIO %d", CONFIG_LED_DRIVER_ONBOARD_GPIO_NUM);

    led_indicator_strips_config_t strips_config = {
        .led_strip_cfg = {
            .strip_gpio_num = CONFIG_LED_DRIVER_ONBOARD_GPIO_NUM,
            .max_leds = 1,
            .led_model = LED_MODEL_WS2812,
            .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
            .flags = { .invert_out = false },
        },
        .led_strip_driver = LED_STRIP_RMT,
    };

    led_indicator_config_t config = {
        .mode = LED_STRIPS_MODE,
        .led_indicator_strips_config = &strips_config,
        .blink_lists = NULL,
        .blink_list_num = 0,
    };

    s_ctx.onboard_handle = led_indicator_create(&config);
    if (s_ctx.onboard_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create onboard LED indicator");
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return ESP_FAIL;
    }

    s_ctx.caps.supports_rgb = true;
    s_ctx.caps.supports_brightness = true;
    ESP_LOGI(TAG, "Onboard WS2812 initialized");

#elif CONFIG_LED_DRIVER_ONBOARD_GPIO
    ESP_LOGI(TAG, "Configuring onboard GPIO LED on GPIO %d", CONFIG_LED_DRIVER_ONBOARD_GPIO_NUM);

    led_indicator_gpio_config_t gpio_config = {
        .gpio_num = CONFIG_LED_DRIVER_ONBOARD_GPIO_NUM,
        .is_active_level_high = true,
    };

    led_indicator_config_t config = {
        .mode = LED_GPIO_MODE,
        .led_indicator_gpio_config = &gpio_config,
        .blink_lists = NULL,
        .blink_list_num = 0,
    };

    s_ctx.onboard_handle = led_indicator_create(&config);
    if (s_ctx.onboard_handle == NULL) {
        ESP_LOGE(TAG, "Failed to create onboard GPIO LED indicator");
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return ESP_FAIL;
    }

    // GPIO LEDs don't support RGB or brightness control
    s_ctx.caps.supports_rgb = false;
    s_ctx.caps.supports_brightness = false;
    ESP_LOGI(TAG, "Onboard GPIO LED initialized");

#elif CONFIG_LED_DRIVER_ONBOARD_NONE
    ESP_LOGI(TAG, "No onboard LED configured");
    s_ctx.onboard_handle = NULL;
#endif

#if CONFIG_LED_DRIVER_EXTERNAL_ENABLED

#if CONFIG_LED_DRIVER_EXTERNAL_RGB_LEDC
    ESP_LOGI(TAG, "Configuring external RGB LEDC on R=%d, G=%d, B=%d",
             CONFIG_LED_DRIVER_EXTERNAL_GPIO_R,
             CONFIG_LED_DRIVER_EXTERNAL_GPIO_G,
             CONFIG_LED_DRIVER_EXTERNAL_GPIO_B);

    led_indicator_rgb_config_t rgb_config = {
        .is_active_level_high = true,
        .timer_inited = false,
        .timer_num = LEDC_TIMER_1,
        .red_gpio_num = CONFIG_LED_DRIVER_EXTERNAL_GPIO_R,
        .green_gpio_num = CONFIG_LED_DRIVER_EXTERNAL_GPIO_G,
        .blue_gpio_num = CONFIG_LED_DRIVER_EXTERNAL_GPIO_B,
        .red_channel = LEDC_CHANNEL_1,
        .green_channel = LEDC_CHANNEL_2,
        .blue_channel = LEDC_CHANNEL_3,
    };

    led_indicator_config_t ext_config = {
        .mode = LED_RGB_MODE,
        .led_indicator_rgb_config = &rgb_config,
        .blink_lists = NULL,
        .blink_list_num = 0,
    };

    s_ctx.external_handle = led_indicator_create(&ext_config);
    if (s_ctx.external_handle == NULL) {
        ESP_LOGW(TAG, "Failed to create external RGB LEDC indicator (continuing with onboard only)");
    } else {
        // External RGB LEDC supports full RGB and brightness
        s_ctx.caps.supports_rgb = true;
        s_ctx.caps.supports_brightness = true;
        ESP_LOGI(TAG, "External RGB LEDC initialized");
    }

#elif CONFIG_LED_DRIVER_EXTERNAL_MONO_LEDC
    ESP_LOGI(TAG, "Configuring external MONO LEDC on GPIO %d", CONFIG_LED_DRIVER_EXTERNAL_GPIO_R);

    led_indicator_ledc_config_t ledc_config = {
        .is_active_level_high = true,
        .timer_inited = false,
        .timer_num = LEDC_TIMER_1,
        .gpio_num = CONFIG_LED_DRIVER_EXTERNAL_GPIO_R,
        .channel = LEDC_CHANNEL_1,
    };

    led_indicator_config_t ext_config = {
        .mode = LED_LEDC_MODE,
        .led_indicator_ledc_config = &ledc_config,
        .blink_lists = NULL,
        .blink_list_num = 0,
    };

    s_ctx.external_handle = led_indicator_create(&ext_config);
    if (s_ctx.external_handle == NULL) {
        ESP_LOGW(TAG, "Failed to create external MONO LEDC indicator");
    } else {
        // MONO LEDC supports brightness but not RGB
        s_ctx.caps.supports_brightness = true;
        ESP_LOGI(TAG, "External MONO LEDC initialized");
    }

#elif CONFIG_LED_DRIVER_EXTERNAL_GPIO
    ESP_LOGI(TAG, "Configuring external GPIO LED on GPIO %d", CONFIG_LED_DRIVER_EXTERNAL_GPIO_R);

    led_indicator_gpio_config_t ext_gpio_config = {
        .is_active_level_high = true,
        .gpio_num = CONFIG_LED_DRIVER_EXTERNAL_GPIO_R,
    };

    led_indicator_config_t ext_config = {
        .mode = LED_GPIO_MODE,
        .led_indicator_gpio_config = &ext_gpio_config,
        .blink_lists = NULL,
        .blink_list_num = 0,
    };

    s_ctx.external_handle = led_indicator_create(&ext_config);
    if (s_ctx.external_handle == NULL) {
        ESP_LOGW(TAG, "Failed to create external GPIO LED indicator");
    } else {
        // GPIO doesn't support RGB or brightness
        ESP_LOGI(TAG, "External GPIO LED initialized");
    }
#endif // External LED type selection

#endif // CONFIG_LED_DRIVER_EXTERNAL_ENABLED

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "LED driver initialized (RGB: %s, Brightness: %s)",
             s_ctx.caps.supports_rgb ? "yes" : "no",
             s_ctx.caps.supports_brightness ? "yes" : "no");

    return ESP_OK;
}

esp_err_t led_driver_get_caps(led_driver_caps_t *caps)
{
    if (caps == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    *caps = s_ctx.caps;
    return ESP_OK;
}

esp_err_t led_driver_set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    // Apply brightness scaling
    uint8_t br = (uint8_t)((r * s_ctx.brightness) / 100);
    uint8_t bg = (uint8_t)((g * s_ctx.brightness) / 100);
    uint8_t bb = (uint8_t)((b * s_ctx.brightness) / 100);

    ESP_LOGD(TAG, "Setting RGB: r=%d, g=%d, b=%d (scaled: r=%d, g=%d, b=%d)",
             r, g, b, br, bg, bb);

    esp_err_t ret = ESP_OK;

    if (s_ctx.onboard_handle) {
#if CONFIG_LED_DRIVER_ONBOARD_WS2812
        ret = led_indicator_set_rgb(s_ctx.onboard_handle, SET_IRGB(0, br, bg, bb));
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set onboard LED RGB: %s", esp_err_to_name(ret));
        }
#elif CONFIG_LED_DRIVER_ONBOARD_GPIO
        // For GPIO, any non-zero RGB turns LED on
        if (r > 0 || g > 0 || b > 0) {
            led_indicator_set_on_off(s_ctx.onboard_handle, true);
        } else {
            led_indicator_set_on_off(s_ctx.onboard_handle, false);
        }
#endif
    }

    // External LED
    if (s_ctx.external_handle) {
#if CONFIG_LED_DRIVER_EXTERNAL_RGB_LEDC
        esp_err_t ext_ret = led_indicator_set_rgb(s_ctx.external_handle, SET_IRGB(0, br, bg, bb));
        if (ext_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set external LED RGB: %s", esp_err_to_name(ext_ret));
            if (ret == ESP_OK) ret = ext_ret;
        }
#elif CONFIG_LED_DRIVER_EXTERNAL_MONO_LEDC
        // Convert to luminance and set brightness
        uint8_t lum = rgb_to_luminance(br, bg, bb);
        uint8_t brightness_pct = (lum * 100) / 255;
        esp_err_t ext_ret = led_indicator_set_brightness(s_ctx.external_handle, brightness_pct);
        if (ext_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set external LED brightness: %s", esp_err_to_name(ext_ret));
            if (ret == ESP_OK) ret = ext_ret;
        }
#elif CONFIG_LED_DRIVER_EXTERNAL_GPIO
        // GPIO: any non-zero = on
        bool on = (br > 0 || bg > 0 || bb > 0);
        esp_err_t ext_ret = led_indicator_set_on_off(s_ctx.external_handle, on);
        if (ext_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set external LED on/off: %s", esp_err_to_name(ext_ret));
            if (ret == ESP_OK) ret = ext_ret;
        }
#endif
    }

    xSemaphoreGive(s_ctx.mutex);
    return ret;
}

esp_err_t led_driver_set_brightness(uint8_t percent)
{
    if (percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    s_ctx.brightness = percent;
    ESP_LOGD(TAG, "Brightness set to %d%%", percent);

    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

esp_err_t led_driver_off(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Turning LED off");

    // Set RGB to 0,0,0 which will turn off the LED
    return led_driver_set_rgb(0, 0, 0);
}

esp_err_t led_driver_deinit(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing LED driver");

    // Turn off LED before deinit
    led_driver_off();

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    // Delete LED indicator handles
    if (s_ctx.onboard_handle) {
        led_indicator_delete(s_ctx.onboard_handle);
        s_ctx.onboard_handle = NULL;
    }

    if (s_ctx.external_handle) {
        led_indicator_delete(s_ctx.external_handle);
        s_ctx.external_handle = NULL;
    }

    // Reset pattern state
    s_ctx.patterns = NULL;
    s_ctx.num_patterns = 0;
    s_ctx.patterns_registered = false;

    s_ctx.initialized = false;

    xSemaphoreGive(s_ctx.mutex);

    // Delete mutex after releasing it
    vSemaphoreDelete(s_ctx.mutex);
    s_ctx.mutex = NULL;

    ESP_LOGI(TAG, "LED driver deinitialized");
    return ESP_OK;
}

esp_err_t led_driver_register_patterns(blink_step_t const **patterns, size_t num_patterns)
{
    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (patterns == NULL || num_patterns == 0) {
        ESP_LOGE(TAG, "Invalid patterns argument");
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    s_ctx.patterns = patterns;
    s_ctx.num_patterns = num_patterns;

    // Recreate LED indicators with pattern support
    esp_err_t ret = recreate_indicators_with_patterns();
    if (ret == ESP_OK) {
        s_ctx.patterns_registered = true;
        ESP_LOGI(TAG, "Registered %zu patterns", num_patterns);
    }

    xSemaphoreGive(s_ctx.mutex);
    return ret;
}

esp_err_t led_driver_start_pattern(int pattern_id)
{
    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!s_ctx.patterns_registered) {
        ESP_LOGE(TAG, "Patterns not registered - call led_driver_register_patterns first");
        return ESP_ERR_INVALID_STATE;
    }

    if (pattern_id < 0 || (size_t)pattern_id >= s_ctx.num_patterns) {
        ESP_LOGE(TAG, "Pattern ID %d out of range (0-%zu)", pattern_id, s_ctx.num_patterns - 1);
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    esp_err_t ret = ESP_OK;

    if (s_ctx.onboard_handle) {
        ret = led_indicator_start(s_ctx.onboard_handle, pattern_id);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start onboard pattern %d: %s", pattern_id, esp_err_to_name(ret));
        }
    }

    if (s_ctx.external_handle) {
        esp_err_t ext_ret = led_indicator_start(s_ctx.external_handle, pattern_id);
        if (ext_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start external pattern %d: %s", pattern_id, esp_err_to_name(ext_ret));
            if (ret == ESP_OK) ret = ext_ret;
        }
    }

    xSemaphoreGive(s_ctx.mutex);
    return ret;
}

esp_err_t led_driver_stop_pattern(int pattern_id)
{
    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    esp_err_t ret = ESP_OK;

    if (s_ctx.onboard_handle) {
        ret = led_indicator_stop(s_ctx.onboard_handle, pattern_id);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop onboard pattern %d: %s", pattern_id, esp_err_to_name(ret));
        }
    }

    if (s_ctx.external_handle) {
        esp_err_t ext_ret = led_indicator_stop(s_ctx.external_handle, pattern_id);
        if (ext_ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to stop external pattern %d: %s", pattern_id, esp_err_to_name(ext_ret));
            if (ret == ESP_OK) ret = ext_ret;
        }
    }

    xSemaphoreGive(s_ctx.mutex);
    return ret;
}

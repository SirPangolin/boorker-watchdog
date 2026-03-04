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

static const char *TAG = "led_driver";

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
} led_driver_ctx_t;

static led_driver_ctx_t s_ctx = {0};

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

    s_ctx.initialized = false;

    xSemaphoreGive(s_ctx.mutex);

    // Delete mutex after releasing it
    vSemaphoreDelete(s_ctx.mutex);
    s_ctx.mutex = NULL;

    ESP_LOGI(TAG, "LED driver deinitialized");
    return ESP_OK;
}

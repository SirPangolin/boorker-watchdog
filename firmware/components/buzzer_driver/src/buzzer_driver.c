/**
 * @file buzzer_driver.c
 * @brief Hardware abstraction for piezo buzzer
 *
 * Uses LEDC PWM for volume control via duty cycle.
 * Active buzzer with low-level trigger (GPIO LOW = sound).
 */

#include "buzzer_driver.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

static const char *TAG = "buzzer_driver";

/**
 * @brief Internal driver context
 *
 * Thread-safe: All public functions acquire mutex before accessing state.
 * Called from both FreeRTOS task context and esp_timer callback context.
 */
typedef struct {
    SemaphoreHandle_t mutex; /**< Protects concurrent access */
    uint8_t volume;          /**< Current volume (0-100) */
    bool is_on;              /**< Current on/off state */
    bool initialized;        /**< Initialization state */
} buzzer_driver_ctx_t;

static buzzer_driver_ctx_t s_ctx = {0};

/**
 * @brief Low-level trigger duty cycle mapping
 *
 * Active buzzer with low-level trigger:
 * - duty=0   → GPIO always LOW  → full volume
 * - duty=255 → GPIO always HIGH → off
 * - Map: volume 0% → duty 255, volume 100% → duty 0
 */
static inline uint32_t volume_to_duty(uint8_t volume)
{
    return 255 - (volume * 255 / 100);
}

esp_err_t buzzer_driver_init(void)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing buzzer driver on GPIO %d", CONFIG_BUZZER_DRIVER_GPIO);

    // Create mutex for thread safety
    s_ctx.mutex = xSemaphoreCreateMutex();
    if (s_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Configure LEDC timer
    ledc_timer_config_t timer_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .timer_num = CONFIG_BUZZER_DRIVER_LEDC_TIMER,
        .duty_resolution = LEDC_TIMER_8_BIT,
        .freq_hz = CONFIG_BUZZER_DRIVER_PWM_FREQ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    esp_err_t err = ledc_timer_config(&timer_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return ESP_FAIL;
    }

    // Configure LEDC channel - start with GPIO HIGH (buzzer off)
    ledc_channel_config_t channel_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = CONFIG_BUZZER_DRIVER_LEDC_CHANNEL,
        .timer_sel = CONFIG_BUZZER_DRIVER_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = CONFIG_BUZZER_DRIVER_GPIO,
        .duty = 255,  // GPIO HIGH = buzzer off (see volume_to_duty)
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return ESP_FAIL;
    }

    // Set default state - ear-safe 2% volume for development
    s_ctx.volume = 2;
    s_ctx.is_on = false;
    s_ctx.initialized = true;

    ESP_LOGI(TAG, "Buzzer driver initialized");
    return ESP_OK;
}

esp_err_t buzzer_driver_deinit(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing buzzer driver");

    // Acquire mutex with longer timeout for deinit
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(500)) != pdTRUE) {
        ESP_LOGE(TAG, "Mutex timeout during deinit");
        return ESP_ERR_TIMEOUT;
    }

    // Ensure buzzer is off before deinit (inline to avoid re-acquiring mutex)
    s_ctx.is_on = false;
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, CONFIG_BUZZER_DRIVER_LEDC_CHANNEL, 255);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set duty off: %s", esp_err_to_name(err));
    }
    ledc_update_duty(LEDC_LOW_SPEED_MODE, CONFIG_BUZZER_DRIVER_LEDC_CHANNEL);

    // Stop LEDC channel with idle HIGH (buzzer off)
    err = ledc_stop(LEDC_LOW_SPEED_MODE, CONFIG_BUZZER_DRIVER_LEDC_CHANNEL, 1);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to stop LEDC channel: %s", esp_err_to_name(err));
    }

    s_ctx.initialized = false;

    xSemaphoreGive(s_ctx.mutex);
    vSemaphoreDelete(s_ctx.mutex);
    s_ctx.mutex = NULL;

    ESP_LOGI(TAG, "Buzzer driver deinitialized");
    return ESP_OK;
}

esp_err_t buzzer_driver_set_volume(uint8_t percent)
{
    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (percent > 100) {
        ESP_LOGE(TAG, "Invalid volume: %d (must be 0-100)", percent);
        return ESP_ERR_INVALID_ARG;
    }

    // Use short timeout - allows retry from timer callback reschedule
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex busy in set_volume");
        return ESP_ERR_TIMEOUT;
    }

    s_ctx.volume = percent;
    ESP_LOGD(TAG, "Volume set to %d%%", percent);

    esp_err_t ret = ESP_OK;

    // If buzzer is on, update LEDC duty cycle immediately
    if (s_ctx.is_on) {
        uint32_t duty = volume_to_duty(s_ctx.volume);

        esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, CONFIG_BUZZER_DRIVER_LEDC_CHANNEL, duty);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set duty: %s", esp_err_to_name(err));
            ret = err;
        } else {
            err = ledc_update_duty(LEDC_LOW_SPEED_MODE, CONFIG_BUZZER_DRIVER_LEDC_CHANNEL);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to update duty: %s", esp_err_to_name(err));
                ret = err;
            }
        }
    }

    xSemaphoreGive(s_ctx.mutex);
    return ret;
}

uint8_t buzzer_driver_get_volume(void)
{
    if (!s_ctx.initialized) {
        return 0;  // Safe default when not initialized
    }
    // Reading a uint8_t is atomic on ESP32, no mutex needed for read-only
    return s_ctx.volume;
}

esp_err_t buzzer_driver_on(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Use short timeout - allows retry from timer callback reschedule
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex busy in on");
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGD(TAG, "Buzzer on (volume: %d%%)", s_ctx.volume);
    s_ctx.is_on = true;

    uint32_t duty = volume_to_duty(s_ctx.volume);

    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, CONFIG_BUZZER_DRIVER_LEDC_CHANNEL, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set duty: %s", esp_err_to_name(err));
        xSemaphoreGive(s_ctx.mutex);
        return err;
    }
    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, CONFIG_BUZZER_DRIVER_LEDC_CHANNEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update duty: %s", esp_err_to_name(err));
        xSemaphoreGive(s_ctx.mutex);
        return err;
    }

    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

esp_err_t buzzer_driver_off(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Use short timeout - allows retry from timer callback reschedule
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(10)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex busy in off");
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGD(TAG, "Buzzer off");
    s_ctx.is_on = false;

    // duty=255 → GPIO HIGH → buzzer off (see volume_to_duty)
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, CONFIG_BUZZER_DRIVER_LEDC_CHANNEL, 255);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set duty: %s", esp_err_to_name(err));
        xSemaphoreGive(s_ctx.mutex);
        return err;
    }
    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, CONFIG_BUZZER_DRIVER_LEDC_CHANNEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update duty: %s", esp_err_to_name(err));
        xSemaphoreGive(s_ctx.mutex);
        return err;
    }

    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

bool buzzer_driver_is_on(void)
{
    if (!s_ctx.initialized) {
        return false;  // Safe default when not initialized
    }
    // Reading a bool is atomic on ESP32, no mutex needed for read-only
    return s_ctx.is_on;
}

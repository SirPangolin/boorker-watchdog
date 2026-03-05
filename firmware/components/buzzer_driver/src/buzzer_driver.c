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
#include "sdkconfig.h"

static const char *TAG = "buzzer_driver";

/**
 * @brief Internal driver context
 */
typedef struct {
    uint8_t volume;         /**< Current volume (0-100) */
    bool is_on;             /**< Current on/off state */
    bool initialized;       /**< Initialization state */
} buzzer_driver_ctx_t;

static buzzer_driver_ctx_t s_ctx = {0};

esp_err_t buzzer_driver_init(void)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing buzzer driver on GPIO %d", CONFIG_BUZZER_DRIVER_GPIO);

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
        return ESP_FAIL;
    }

    // Configure LEDC channel - start with GPIO HIGH (buzzer off)
    ledc_channel_config_t channel_conf = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = CONFIG_BUZZER_DRIVER_LEDC_CHANNEL,
        .timer_sel = CONFIG_BUZZER_DRIVER_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = CONFIG_BUZZER_DRIVER_GPIO,
        .duty = 255,  // GPIO HIGH = buzzer off (low-level trigger)
        .hpoint = 0,
    };
    err = ledc_channel_config(&channel_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(err));
        return ESP_FAIL;
    }

    // Set default state - ear-safe volume for development
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

    // Ensure buzzer is off before deinit
    buzzer_driver_off();

    // Stop LEDC channel with idle HIGH (buzzer off)
    ledc_stop(LEDC_LOW_SPEED_MODE, CONFIG_BUZZER_DRIVER_LEDC_CHANNEL, 1);  // 1 = idle HIGH

    s_ctx.initialized = false;

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

    s_ctx.volume = percent;
    ESP_LOGD(TAG, "Volume set to %d%%", percent);

    // If buzzer is on, update LEDC duty cycle immediately
    if (s_ctx.is_on) {
        // Low-level trigger: duty=0 means GPIO always LOW = full volume
        // duty=255 means GPIO always HIGH = off
        // Map: volume 0% -> duty 255, volume 100% -> duty 0
        uint32_t duty = 255 - (s_ctx.volume * 255 / 100);

        esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, CONFIG_BUZZER_DRIVER_LEDC_CHANNEL, duty);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set duty: %s", esp_err_to_name(err));
            return err;
        }
        err = ledc_update_duty(LEDC_LOW_SPEED_MODE, CONFIG_BUZZER_DRIVER_LEDC_CHANNEL);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to update duty: %s", esp_err_to_name(err));
            return err;
        }
    }

    return ESP_OK;
}

uint8_t buzzer_driver_get_volume(void)
{
    return s_ctx.volume;
}

esp_err_t buzzer_driver_on(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Buzzer on (volume: %d%%)", s_ctx.volume);
    s_ctx.is_on = true;

    // Low-level trigger: duty=0 means GPIO always LOW = full volume
    // duty=255 means GPIO always HIGH = off
    // Map: volume 0% -> duty 255, volume 100% -> duty 0
    uint32_t duty = 255 - (s_ctx.volume * 255 / 100);

    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, CONFIG_BUZZER_DRIVER_LEDC_CHANNEL, duty);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set duty: %s", esp_err_to_name(err));
        return err;
    }
    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, CONFIG_BUZZER_DRIVER_LEDC_CHANNEL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to update duty: %s", esp_err_to_name(err));
        return err;
    }

    return ESP_OK;
}

esp_err_t buzzer_driver_off(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGD(TAG, "Buzzer off");
    s_ctx.is_on = false;

    // Set GPIO HIGH (duty 255) to turn buzzer off (low-level trigger)
    esp_err_t err = ledc_set_duty(LEDC_LOW_SPEED_MODE, CONFIG_BUZZER_DRIVER_LEDC_CHANNEL, 255);
    if (err != ESP_OK) return err;
    err = ledc_update_duty(LEDC_LOW_SPEED_MODE, CONFIG_BUZZER_DRIVER_LEDC_CHANNEL);
    if (err != ESP_OK) return err;

    return ESP_OK;
}

bool buzzer_driver_is_on(void)
{
    return s_ctx.is_on;
}

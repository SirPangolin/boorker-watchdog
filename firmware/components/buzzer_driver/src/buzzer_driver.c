/**
 * @file buzzer_driver.c
 * @brief Hardware abstraction for piezo buzzer
 *
 * Uses LEDC PWM for volume control via duty cycle.
 * Active buzzer with low-level trigger (GPIO LOW = sound).
 */

#include "buzzer_driver.h"
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

    // TODO: Task 2 - Configure LEDC timer and channel for PWM
    // - Use CONFIG_BUZZER_DRIVER_LEDC_TIMER
    // - Use CONFIG_BUZZER_DRIVER_LEDC_CHANNEL
    // - Use CONFIG_BUZZER_DRIVER_PWM_FREQ
    // - Low-level trigger: duty cycle inverted (100% duty = off, 0% duty = full volume)

    // Set default state
    s_ctx.volume = 100;
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

    // TODO: Task 2 - Stop LEDC and reset GPIO

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

    // TODO: Task 2 - If buzzer is on, update LEDC duty cycle

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

    // TODO: Task 2 - Set LEDC duty cycle based on volume
    // Low-level trigger: inverted duty (100% volume = 0% duty = GPIO LOW)

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

    // TODO: Task 2 - Set LEDC to 100% duty (GPIO HIGH = buzzer off)

    return ESP_OK;
}

bool buzzer_driver_is_on(void)
{
    return s_ctx.is_on;
}

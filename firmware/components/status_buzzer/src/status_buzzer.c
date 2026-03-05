/**
 * @file status_buzzer.c
 * @brief Status buzzer core implementation (event bus channel subscriber)
 *
 * Maps system states to buzzer patterns via buzzer_driver component
 * for hardware abstraction. Subscribes to event_bus for state
 * notifications and plays appropriate transition sounds.
 */

#include "status_buzzer.h"
#include "buzzer_driver.h"
#include "event_bus.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char *TAG = "status_buzzer";

// Preset volume storage (initialized from Kconfig defaults)
static uint8_t s_preset_volumes[BUZZER_PRESET_MAX] = {
    [BUZZER_PRESET_CHIRP]       = CONFIG_STATUS_BUZZER_VOLUME_CHIRP,
    [BUZZER_PRESET_DOUBLE_BEEP] = CONFIG_STATUS_BUZZER_VOLUME_DOUBLE_BEEP,
    [BUZZER_PRESET_TRIPLE_BEEP] = CONFIG_STATUS_BUZZER_VOLUME_TRIPLE_BEEP,
    [BUZZER_PRESET_ALARM]       = CONFIG_STATUS_BUZZER_VOLUME_ALARM,
    [BUZZER_PRESET_SOLID]       = CONFIG_STATUS_BUZZER_VOLUME_SOLID,
};

// Preset names for logging
static const char *s_preset_names[] = {
    [BUZZER_PRESET_CHIRP]       = "CHIRP",
    [BUZZER_PRESET_DOUBLE_BEEP] = "DOUBLE_BEEP",
    [BUZZER_PRESET_TRIPLE_BEEP] = "TRIPLE_BEEP",
    [BUZZER_PRESET_ALARM]       = "ALARM",
    [BUZZER_PRESET_SOLID]       = "SOLID",
};

// Static state
static struct {
    bool initialized;
    bool enabled;
    bool alerts_only;
} s_buzzer = {
    .initialized = false,
    .enabled = true,
    .alerts_only = false,
};

esp_err_t status_buzzer_init(void)
{
    if (s_buzzer.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // TODO: Initialize buzzer driver
    // TODO: Load config from NVS
    // TODO: Register as event bus channel

    s_buzzer.initialized = true;
    ESP_LOGI(TAG, "Initialized");

    return ESP_OK;
}

esp_err_t status_buzzer_deinit(void)
{
    if (!s_buzzer.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // TODO: Stop any playing sound
    // TODO: Deinitialize buzzer driver

    s_buzzer.initialized = false;
    ESP_LOGI(TAG, "Deinitialized");

    return ESP_OK;
}

esp_err_t status_buzzer_set_enabled(bool enabled)
{
    if (!s_buzzer.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_buzzer.enabled = enabled;
    ESP_LOGI(TAG, "Status buzzer %s", enabled ? "enabled" : "disabled");

    return ESP_OK;
}

bool status_buzzer_is_enabled(void)
{
    return s_buzzer.enabled;
}

esp_err_t status_buzzer_set_alerts_only(bool alerts_only)
{
    if (!s_buzzer.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_buzzer.alerts_only = alerts_only;
    ESP_LOGI(TAG, "Alerts-only mode %s", alerts_only ? "enabled" : "disabled");

    return ESP_OK;
}

bool status_buzzer_is_alerts_only(void)
{
    return s_buzzer.alerts_only;
}

esp_err_t status_buzzer_set_preset_volume(buzzer_preset_t preset, uint8_t percent)
{
    if (preset >= BUZZER_PRESET_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    s_preset_volumes[preset] = percent;
    ESP_LOGI(TAG, "Preset %s volume set to %d%%", s_preset_names[preset], percent);

    return ESP_OK;
}

uint8_t status_buzzer_get_preset_volume(buzzer_preset_t preset)
{
    if (preset >= BUZZER_PRESET_MAX) {
        return 0;
    }
    return s_preset_volumes[preset];
}

esp_err_t status_buzzer_play(buzzer_preset_t preset)
{
    if (!s_buzzer.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (preset >= BUZZER_PRESET_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_buzzer.enabled) {
        return ESP_OK;  // Silently succeed when disabled
    }

    // TODO: Implement pattern playback
    ESP_LOGI(TAG, "Playing preset: %s (volume=%d%%)",
             s_preset_names[preset], s_preset_volumes[preset]);

    return ESP_OK;
}

esp_err_t status_buzzer_stop(void)
{
    if (!s_buzzer.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // TODO: Stop any playing pattern
    ESP_LOGD(TAG, "Stopped");

    return ESP_OK;
}

esp_err_t status_buzzer_save_config(void)
{
    if (!s_buzzer.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // TODO: Save config to NVS
    ESP_LOGI(TAG, "Config saved (stub)");

    return ESP_OK;
}

esp_err_t status_buzzer_register_console(void)
{
    // TODO: Register console commands
    ESP_LOGI(TAG, "Console commands registered (stub)");

    return ESP_OK;
}

const char *buzzer_preset_name(buzzer_preset_t preset)
{
    if (preset >= BUZZER_PRESET_MAX) {
        return "UNKNOWN";
    }
    return s_preset_names[preset];
}

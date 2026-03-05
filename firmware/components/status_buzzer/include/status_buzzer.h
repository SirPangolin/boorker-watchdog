/**
 * @file status_buzzer.h
 * @brief Audio feedback via piezo buzzer
 *
 * Subscribes to event_bus and plays transition sounds when states change.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    BUZZER_PRESET_CHIRP = 0,
    BUZZER_PRESET_DOUBLE_BEEP,
    BUZZER_PRESET_TRIPLE_BEEP,
    BUZZER_PRESET_ALARM,
    BUZZER_PRESET_SOLID,
    BUZZER_PRESET_MAX
} buzzer_preset_t;

esp_err_t status_buzzer_init(void);
esp_err_t status_buzzer_deinit(void);
esp_err_t status_buzzer_set_enabled(bool enabled);
bool status_buzzer_is_enabled(void);
esp_err_t status_buzzer_set_alerts_only(bool alerts_only);
bool status_buzzer_is_alerts_only(void);
esp_err_t status_buzzer_set_preset_volume(buzzer_preset_t preset, uint8_t percent);
uint8_t status_buzzer_get_preset_volume(buzzer_preset_t preset);
esp_err_t status_buzzer_play(buzzer_preset_t preset);
esp_err_t status_buzzer_stop(void);
esp_err_t status_buzzer_save_config(void);
esp_err_t status_buzzer_register_console(void);
const char *buzzer_preset_name(buzzer_preset_t preset);

#ifdef __cplusplus
}
#endif

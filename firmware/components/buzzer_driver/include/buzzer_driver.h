/**
 * @file buzzer_driver.h
 * @brief Hardware abstraction for piezo buzzer
 *
 * Uses LEDC PWM for volume control via duty cycle.
 * Active buzzer with low-level trigger (GPIO LOW = sound).
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t buzzer_driver_init(void);
esp_err_t buzzer_driver_deinit(void);
esp_err_t buzzer_driver_set_volume(uint8_t percent);
uint8_t buzzer_driver_get_volume(void);
esp_err_t buzzer_driver_on(void);
esp_err_t buzzer_driver_off(void);
bool buzzer_driver_is_on(void);

#ifdef __cplusplus
}
#endif

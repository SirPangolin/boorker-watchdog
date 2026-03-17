/**
 * @file display_hal.h
 * @brief u8g2 HAL callbacks and init for ESP-IDF I2C
 */

#pragma once

#include "esp_err.h"
#include "sdkconfig.h"

#if CONFIG_STATUS_DISPLAY_ENABLED

#include "u8g2.h"

#ifdef __cplusplus
extern "C" {
#endif

/** u8g2 I2C byte callback — handles I2C start/send/stop */
uint8_t display_hal_i2c_byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

/** u8g2 GPIO and delay callback — handles reset pin and timing */
uint8_t display_hal_gpio_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

/** Initialize I2C bus and reset GPIO */
esp_err_t display_hal_init(void);

/** Deinitialize I2C bus */
esp_err_t display_hal_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* CONFIG_STATUS_DISPLAY_ENABLED */

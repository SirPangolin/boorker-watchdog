/**
 * @file sw420_driver.h
 * @brief Hardware abstraction for SW-420 vibration sensor
 *
 * Handle-based API for future multi-instance support.
 * v1 limits to single instance.
 */

#ifndef SW420_DRIVER_H
#define SW420_DRIVER_H

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sw420_inst* sw420_handle_t;

/**
 * @brief Create SW-420 driver instance
 *
 * @note v1: Only one instance supported. Returns ESP_ERR_INVALID_STATE if
 *       instance already exists.
 *
 * @param gpio GPIO pin connected to SW-420 DO
 * @param[out] out_handle Output handle
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if instance already exists
 * @return ESP_ERR_INVALID_ARG if gpio invalid
 * @return ESP_ERR_NO_MEM if allocation fails
 */
esp_err_t sw420_driver_create(gpio_num_t gpio, sw420_handle_t *out_handle);

/**
 * @brief Destroy SW-420 driver instance
 *
 * @param handle Instance handle (NULL safe)
 * @return ESP_OK on success
 */
esp_err_t sw420_driver_destroy(sw420_handle_t handle);

/**
 * @brief Read debounced vibration state
 *
 * @param handle Instance handle
 * @param[out] vibrating true if vibrating, false if idle
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if handle invalid
 * @return ESP_ERR_INVALID_ARG if vibrating is NULL
 */
esp_err_t sw420_driver_read(sw420_handle_t handle, bool *vibrating);

/**
 * @brief Read raw GPIO state (for calibration)
 *
 * Bypasses debounce logic. Use for live calibration feedback.
 *
 * @param handle Instance handle
 * @return true if GPIO indicates vibration, false otherwise
 */
bool sw420_driver_read_raw(sw420_handle_t handle);

/**
 * @brief Set debounce configuration
 *
 * @param handle Instance handle
 * @param debounce_on_ms Vibration sustain time to register active
 * @param debounce_off_ms Quiet time to register idle
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if handle invalid
 */
esp_err_t sw420_driver_set_config(sw420_handle_t handle,
                                   uint32_t debounce_on_ms,
                                   uint32_t debounce_off_ms);

/**
 * @brief Get debounce configuration
 *
 * @param handle Instance handle
 * @param[out] debounce_on_ms Output ON threshold (NULL to skip)
 * @param[out] debounce_off_ms Output OFF threshold (NULL to skip)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if handle invalid
 */
esp_err_t sw420_driver_get_config(sw420_handle_t handle,
                                   uint32_t *debounce_on_ms,
                                   uint32_t *debounce_off_ms);

/**
 * @brief Save current config to NVS
 *
 * @param handle Instance handle
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if handle invalid
 * @return ESP_FAIL on NVS error
 */
esp_err_t sw420_driver_save_config(sw420_handle_t handle);

/**
 * @brief Load config from NVS (called automatically by create)
 *
 * @param handle Instance handle
 * @return ESP_OK on success (or no saved config, uses defaults)
 */
esp_err_t sw420_driver_load_config(sw420_handle_t handle);

/**
 * @brief Get singleton instance
 *
 * @note v1: Returns the single instance created by sw420_driver_create
 *
 * @return Instance handle, or NULL if not created
 */
sw420_handle_t sw420_driver_get_instance(void);

/**
 * @brief Register console commands
 *
 * Registers: vibration [status|raw|config]
 *
 * @param handle Instance handle (NULL to use singleton)
 * @return ESP_OK on success
 */
esp_err_t sw420_driver_register_console(sw420_handle_t handle);

#ifdef __cplusplus
}
#endif

#endif /* SW420_DRIVER_H */

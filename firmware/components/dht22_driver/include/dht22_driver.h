/**
 * @file dht22_driver.h
 * @brief Hardware abstraction for DHT22 temperature/humidity sensor
 *
 * Uses am2302_rmt library for reliable RMT-based communication.
 * Temperature is returned in Fahrenheit.
 *
 * @note This driver is thread-safe. All public functions use mutex protection.
 */

#ifndef DHT22_DRIVER_H
#define DHT22_DRIVER_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize the DHT22 driver
 *
 * Creates RMT-based sensor handle using configured GPIO.
 * Must be called before any read operations.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if already initialized
 * @return ESP_ERR_NO_MEM if memory allocation fails
 * @return ESP_FAIL on hardware initialization failure
 */
esp_err_t dht22_driver_init(void);

/**
 * @brief Deinitialize the DHT22 driver
 *
 * Releases sensor handle and frees resources.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex acquisition fails
 * @return ESP_FAIL if sensor cleanup fails (resources may leak)
 */
esp_err_t dht22_driver_deinit(void);

/**
 * @brief Read temperature and humidity from DHT22
 *
 * Performs a blocking read from the sensor.
 *
 * @note DHT22 requires minimum 2 seconds between reads.
 *       Caller is responsible for rate limiting.
 *
 * @param[out] temperature_f Temperature in Fahrenheit (NULL to skip)
 * @param[out] humidity_pct Relative humidity percentage (NULL to skip)
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_INVALID_ARG if both pointers are NULL
 * @return ESP_ERR_TIMEOUT if mutex acquisition fails
 * @return ESP_ERR_INVALID_RESPONSE if sensor returns out-of-range values
 * @return ESP_FAIL on read failure
 */
esp_err_t dht22_driver_read(float *temperature_f, float *humidity_pct);

/**
 * @brief Check if driver is initialized
 *
 * @return true if initialized, false otherwise
 */
bool dht22_driver_is_initialized(void);

#ifdef __cplusplus
}
#endif

#endif /* DHT22_DRIVER_H */

/**
 * @file sensor_manager.h
 * @brief High-level sensor orchestration
 *
 * Manages sensor registry, polling task, and callbacks.
 * Handles graceful reconnection for sensor disconnect/reconnect.
 * Boot-time configuration from NVS.
 *
 * @note This component is thread-safe. All public functions use mutex protection.
 */

#ifndef SENSOR_MANAGER_H
#define SENSOR_MANAGER_H

#include "esp_err.h"
#include "sensor_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize sensor manager
 *
 * Loads sensor configuration from NVS and initializes configured drivers.
 * If no NVS config exists, uses Kconfig defaults.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if already initialized
 * @return ESP_ERR_NO_MEM if mutex allocation fails
 */
esp_err_t sensor_manager_init(void);

/**
 * @brief Deinitialize sensor manager
 *
 * Stops polling task, deinitializes drivers, releases resources.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t sensor_manager_deinit(void);

/**
 * @brief Start sensor polling task
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized or already running
 * @return ESP_ERR_NO_MEM if task creation fails
 */
esp_err_t sensor_manager_start(void);

/**
 * @brief Stop sensor polling task
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not running
 */
esp_err_t sensor_manager_stop(void);

/**
 * @brief Get last reading for a sensor
 *
 * @param sensor_id Sensor identifier
 * @param out Output reading (copied)
 * @return ESP_OK on success
 * @return ESP_ERR_NOT_FOUND if sensor not configured
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_INVALID_ARG if sensor_id or out is NULL
 */
esp_err_t sensor_manager_get_reading(const char *sensor_id, sensor_reading_t *out);

/**
 * @brief Get sensor status
 *
 * @param sensor_id Sensor identifier
 * @return Sensor status, or SENSOR_STATUS_ERROR if not found
 */
sensor_status_t sensor_manager_get_status(const char *sensor_id);

/**
 * @brief Get number of configured sensors
 */
size_t sensor_manager_get_sensor_count(void);

/**
 * @brief Get sensor reading by index (for display enumeration)
 *
 * Allows iterating sensors without knowing IDs. Index order matches
 * registration order (typically Kconfig default order).
 *
 * @param index Sensor index (0 to get_sensor_count()-1)
 * @param out Output reading (copied under mutex)
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if index out of range or out is NULL
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex acquisition fails
 */
esp_err_t sensor_manager_get_reading_by_index(size_t index, sensor_reading_t *out);

/**
 * @brief Get sensor ID string by index
 *
 * @param index Sensor index (0 to get_sensor_count()-1)
 * @return Sensor ID string (e.g., "temp_humidity"), or NULL if index invalid
 */
const char *sensor_manager_get_sensor_id(size_t index);

/**
 * @brief Register console commands for sensor manager
 *
 * Registers the "sensor" command with subcommands:
 * - sensor: Show all configured sensors
 * - sensor status: Same as bare sensor command
 * - sensor read <id>: Read specific sensor
 *
 * @return ESP_OK on success
 * @return ESP_ERR_NO_MEM if argtable allocation fails
 */
esp_err_t sensor_manager_register_console(void);

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_MANAGER_H */

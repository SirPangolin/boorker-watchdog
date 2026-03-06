/**
 * @file sensor_types.h
 * @brief Shared types for sensor system
 *
 * @note This header is thread-safe. Types are designed for use across tasks.
 */

#ifndef SENSOR_TYPES_H
#define SENSOR_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Sensor status
 */
typedef enum {
    SENSOR_STATUS_ONLINE,      /**< Reads succeeding */
    SENSOR_STATUS_OFFLINE,     /**< Reads failing, retrying */
    SENSOR_STATUS_DISABLED,    /**< User disabled */
    SENSOR_STATUS_ERROR,       /**< Init failed (bad config, etc.) */
} sensor_status_t;

/**
 * @brief Sensor reading
 *
 * @note When obtained via sensor_manager_get_reading(), the sensor_id pointer
 *       points to manager-internal storage and is only valid while the manager
 *       is initialized. Copy the string if persistence is needed.
 */
typedef struct {
    const char *sensor_id;     /**< Sensor identifier (e.g., "temp_humidity") */
    uint32_t timestamp_ms;     /**< Uptime when reading was taken */
    float value;               /**< Primary value (temperature for DHT22, NAN if invalid) */
    float value2;              /**< Secondary value (humidity for DHT22, NAN if unused) */
    sensor_status_t status;    /**< Current sensor status */
} sensor_reading_t;

/**
 * @brief Callback invoked when a sensor reading is available
 *
 * @note Called from sensor_manager task context (not ISR).
 *       Keep callback short and non-blocking.
 *
 * @warning The reading is a copy valid for the callback duration.
 *          Do not store the pointer; copy the struct if persistence is needed.
 *
 * @param reading The sensor reading (copy, valid during callback)
 * @param user_data User context from registration
 */
typedef void (*sensor_callback_t)(const sensor_reading_t *reading, void *user_data);

/**
 * @brief Get status name string
 */
static inline const char *sensor_status_name(sensor_status_t status)
{
    switch (status) {
        case SENSOR_STATUS_ONLINE:   return "ONLINE";
        case SENSOR_STATUS_OFFLINE:  return "OFFLINE";
        case SENSOR_STATUS_DISABLED: return "DISABLED";
        case SENSOR_STATUS_ERROR:    return "ERROR";
        default:                     return "UNKNOWN";
    }
}

#ifdef __cplusplus
}
#endif

#endif /* SENSOR_TYPES_H */

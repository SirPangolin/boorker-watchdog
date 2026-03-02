/**
 * @file device_state.h
 * @brief Device lifecycle state management
 *
 * Manages system lifecycle states including:
 * - Claimed status (whether device password has been changed from default)
 * - Factory reset state machine
 * - OTA update state machine
 *
 * All state changes are persisted to NVS immediately and are thread-safe.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Factory reset state machine
 */
typedef enum {
    DEVICE_FACTORY_RESET_NONE = 0,       /**< No factory reset requested */
    DEVICE_FACTORY_RESET_PENDING,        /**< Reset requested, awaiting confirmation/execution */
    DEVICE_FACTORY_RESET_IN_PROGRESS,    /**< Reset in progress */
} device_factory_reset_t;

/**
 * @brief OTA update state machine
 */
typedef enum {
    DEVICE_OTA_IDLE = 0,         /**< No OTA in progress */
    DEVICE_OTA_DOWNLOADING,      /**< Downloading firmware image */
    DEVICE_OTA_VERIFYING,        /**< Verifying downloaded image */
    DEVICE_OTA_PENDING_REBOOT,   /**< OTA complete, pending reboot to apply */
} device_ota_state_t;

/**
 * @brief Initialize device state component
 *
 * Loads persisted state from NVS. Must be called before other device_state functions.
 * Safe to call multiple times (returns ESP_OK if already initialized).
 *
 * @return ESP_OK on success
 * @return ESP_ERR_NO_MEM if mutex creation fails
 * @return ESP_ERR_NVS_* on NVS errors
 */
esp_err_t device_state_init(void);

/**
 * @brief Deinitialize device state component
 *
 * Releases resources. State remains in NVS.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex cannot be acquired
 */
esp_err_t device_state_deinit(void);

/**
 * @brief Check if device has been claimed
 *
 * A device is "claimed" when the user has changed the default password,
 * indicating they have taken ownership of the device.
 *
 * @return true if device has been claimed
 * @return false if unclaimed or not initialized
 */
bool device_state_is_claimed(void);

/**
 * @brief Set device claimed status
 *
 * @param claimed true to mark device as claimed, false to unclaim
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex cannot be acquired
 * @return ESP_ERR_NVS_* on NVS errors
 */
esp_err_t device_state_set_claimed(bool claimed);

/**
 * @brief Get current factory reset state
 *
 * @return Current factory reset state, or DEVICE_FACTORY_RESET_NONE if not initialized
 */
device_factory_reset_t device_state_get_factory_reset(void);

/**
 * @brief Request a factory reset
 *
 * Sets state to DEVICE_FACTORY_RESET_PENDING. Caller is responsible for
 * transitioning to IN_PROGRESS and executing the actual reset.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex cannot be acquired
 * @return ESP_ERR_NVS_* on NVS errors
 */
esp_err_t device_state_request_factory_reset(void);

/**
 * @brief Set factory reset to in-progress state
 *
 * Call this when actually beginning the factory reset process.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized or not in PENDING state
 * @return ESP_ERR_TIMEOUT if mutex cannot be acquired
 * @return ESP_ERR_NVS_* on NVS errors
 */
esp_err_t device_state_begin_factory_reset(void);

/**
 * @brief Clear factory reset state
 *
 * Sets state back to DEVICE_FACTORY_RESET_NONE. Call after reset completes
 * or to cancel a pending reset.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex cannot be acquired
 * @return ESP_ERR_NVS_* on NVS errors
 */
esp_err_t device_state_clear_factory_reset(void);

/**
 * @brief Get current OTA state
 *
 * @return Current OTA state, or DEVICE_OTA_IDLE if not initialized
 */
device_ota_state_t device_state_get_ota(void);

/**
 * @brief Set OTA state
 *
 * @param state New OTA state
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if state is invalid
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex cannot be acquired
 * @return ESP_ERR_NVS_* on NVS errors
 */
esp_err_t device_state_set_ota(device_ota_state_t state);

/**
 * @brief Get string name for factory reset state
 *
 * @param state Factory reset state
 * @return Human-readable state name
 */
const char *device_state_factory_reset_name(device_factory_reset_t state);

/**
 * @brief Get string name for OTA state
 *
 * @param state OTA state
 * @return Human-readable state name
 */
const char *device_state_ota_name(device_ota_state_t state);

#ifdef __cplusplus
}
#endif

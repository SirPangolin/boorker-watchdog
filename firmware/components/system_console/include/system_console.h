#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register all system console commands
 *
 * Commands: reboot, version, free, uptime
 *
 * @note Thread-safe. Uses internal mutex for state protection.
 */
esp_err_t system_console_register(void);

/**
 * @brief Schedule a system reboot
 *
 * @param delay_seconds Seconds until reboot (0 = immediate)
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_STATE if reboot already pending
 *         ESP_ERR_INVALID_ARG if delay exceeds maximum
 *         ESP_ERR_NO_MEM if timer creation fails
 *
 * @note Thread-safe.
 */
esp_err_t system_reboot_schedule(uint32_t delay_seconds);

/**
 * @brief Cancel a pending reboot
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if no reboot pending
 *
 * @note Thread-safe.
 */
esp_err_t system_reboot_cancel(void);

/**
 * @brief Check if a reboot is pending
 *
 * @note Thread-safe.
 */
bool system_reboot_is_pending(void);

/**
 * @brief Get seconds remaining until reboot
 *
 * @return Seconds remaining, or 0 if no reboot pending
 *
 * @note Thread-safe.
 */
uint32_t system_reboot_get_remaining(void);

#ifdef __cplusplus
}
#endif

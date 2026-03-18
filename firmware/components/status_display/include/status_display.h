/**
 * @file status_display.h
 * @brief Status display component (OLED ANDON channel subscriber)
 *
 * Subscribes to event_bus for state notifications and renders appropriate
 * screens on the SSD1306 OLED display via u8g2 graphics library.
 * Handles button input for screen navigation.
 *
 * Screens: Boot splash, First Boot (QR+creds), Dashboard (auto-rotate
 * sensor nodes), Alert override, Drill-down (network/LoRa/system/nodes/sensors).
 */

#pragma once

#include "esp_err.h"
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_STATUS_DISPLAY_ENABLED

/**
 * @brief Initialize the status display
 *
 * Initializes u8g2 (SSD1306 I2C), button_driver (PRG button),
 * creates the display task, and starts the render loop.
 * Non-blocking — splash runs in the display task.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if already initialized
 * @return ESP_ERR_NO_MEM if task creation fails
 * @return ESP_FAIL on hardware init failure
 */
esp_err_t status_display_init(void);

/**
 * @brief Deinitialize the status display
 */
esp_err_t status_display_deinit(void);

/**
 * @brief Register console commands (disp, disp on/off, etc.)
 */
esp_err_t status_display_register_console(void);

#endif /* CONFIG_STATUS_DISPLAY_ENABLED */

#ifdef __cplusplus
}
#endif

/**
 * @file display_driver.h
 * @brief SSD1306 OLED display hardware abstraction via esp_lcd
 *
 * Manages I2C communication, framebuffer, and display on/off control
 * for a 128x64 (or 128x32) monochrome SSD1306 OLED panel.
 *
 * Framebuffer is in SSD1306 page mode: each byte represents 8 vertical
 * pixels in a column. Bit 0 = topmost pixel of the 8-pixel group.
 *
 * @note Thread Safety: All public functions are mutex-protected.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

#if CONFIG_DISPLAY_DRIVER_ENABLED

#define DISPLAY_WIDTH   CONFIG_DISPLAY_DRIVER_WIDTH
#define DISPLAY_HEIGHT  CONFIG_DISPLAY_DRIVER_HEIGHT
#define DISPLAY_PAGES   (DISPLAY_HEIGHT / 8)
#define DISPLAY_BUF_SIZE (DISPLAY_WIDTH * DISPLAY_PAGES)

/**
 * @brief Initialize the display driver
 *
 * Sets up I2C master bus, creates SSD1306 panel via esp_lcd,
 * allocates framebuffer, resets and enables the display.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if already initialized
 * @return ESP_FAIL on hardware init failure
 */
esp_err_t display_driver_init(void);

/**
 * @brief Deinitialize the display driver
 *
 * Turns off display, deletes panel and I/O handles, releases I2C bus.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t display_driver_deinit(void);

/**
 * @brief Get pointer to the framebuffer
 *
 * The framebuffer is in SSD1306 page mode:
 * - Size: DISPLAY_WIDTH * DISPLAY_PAGES bytes
 * - Byte at offset (page * DISPLAY_WIDTH + x) contains 8 vertical pixels
 *   at column x, rows (page*8) through (page*8+7)
 * - Bit 0 = topmost pixel, bit 7 = bottommost pixel
 *
 * @return Framebuffer pointer, or NULL if not initialized
 */
uint8_t *display_driver_get_framebuffer(void);

/**
 * @brief Clear the framebuffer (all pixels off)
 *
 * @return ESP_OK on success
 */
esp_err_t display_driver_clear(void);

/**
 * @brief Push framebuffer contents to the display
 *
 * Transfers the entire framebuffer to the SSD1306 via I2C.
 * At 400kHz I2C, a full 1KB transfer takes ~25ms.
 *
 * @return ESP_OK on success
 * @return ESP_FAIL on I2C transfer error
 */
esp_err_t display_driver_flush(void);

/**
 * @brief Turn display on (pixels visible)
 *
 * @return ESP_OK on success
 */
esp_err_t display_driver_on(void);

/**
 * @brief Turn display off (all pixels dark, controller still active)
 *
 * @return ESP_OK on success
 */
esp_err_t display_driver_off(void);

/**
 * @brief Set display contrast
 *
 * @param contrast 0-255 (0 = dimmest, 255 = brightest)
 * @return ESP_OK on success
 */
esp_err_t display_driver_set_contrast(uint8_t contrast);

#endif /* CONFIG_DISPLAY_DRIVER_ENABLED */

#ifdef __cplusplus
}
#endif

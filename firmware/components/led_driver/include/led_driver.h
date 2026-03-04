/**
 * @file led_driver.h
 * @brief LED hardware abstraction layer
 *
 * Provides a unified API for controlling various LED hardware types:
 * - WS2812 (addressable RGB)
 * - RGB LEDC (PWM-controlled RGB)
 * - MONO LEDC (PWM-controlled single color)
 * - GPIO (on/off only)
 *
 * Configuration is done via Kconfig options.
 */

#ifndef LED_DRIVER_H
#define LED_DRIVER_H

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED driver capability flags
 */
typedef struct {
    bool supports_rgb;        /**< True if LED supports RGB color control */
    bool supports_brightness; /**< True if LED supports brightness control */
} led_driver_caps_t;

/**
 * @brief Initialize the LED driver
 *
 * Initializes the LED hardware based on Kconfig settings.
 * Must be called before any other led_driver functions.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if already initialized
 *      - ESP_FAIL on hardware initialization failure
 */
esp_err_t led_driver_init(void);

/**
 * @brief Get LED driver capabilities
 *
 * Query what features the configured LED hardware supports.
 *
 * @param[out] caps Pointer to capability structure to fill
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if caps is NULL
 *      - ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t led_driver_get_caps(led_driver_caps_t *caps);

/**
 * @brief Set LED color (RGB)
 *
 * Sets the LED color. For non-RGB LEDs, any non-zero value
 * turns the LED on.
 *
 * @param r Red component (0-255)
 * @param g Green component (0-255)
 * @param b Blue component (0-255)
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t led_driver_set_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * @brief Set LED brightness
 *
 * Sets the LED brightness as a percentage. For GPIO LEDs,
 * 0 = off, >0 = on.
 *
 * @param percent Brightness percentage (0-100)
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_ARG if percent > 100
 *      - ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t led_driver_set_brightness(uint8_t percent);

/**
 * @brief Turn off the LED
 *
 * Turns off all LEDs controlled by this driver.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t led_driver_off(void);

/**
 * @brief Deinitialize the LED driver
 *
 * Releases all resources used by the LED driver.
 * LED will be turned off before deinitialization.
 *
 * @return
 *      - ESP_OK on success
 *      - ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t led_driver_deinit(void);

#ifdef __cplusplus
}
#endif

#endif /* LED_DRIVER_H */

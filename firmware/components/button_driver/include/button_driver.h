/**
 * @file button_driver.h
 * @brief Generic N-button GPIO driver with press detection
 *
 * Supports momentary (short/long/very-long press) and latched (toggle) buttons.
 * Each button is registered at runtime with per-button configuration for GPIO,
 * active level, debounce, press thresholds, and callback.
 *
 * Uses GPIO ISR for edge detection + FreeRTOS timer for debounce/press timing.
 * Callbacks fire from FreeRTOS timer daemon task context — must not block.
 *
 * @note Thread Safety: register/unregister are mutex-protected.
 *       Callbacks fire from timer task — callers must handle their own sync.
 */

#pragma once

#include "esp_err.h"
#include "driver/gpio.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Button operating mode
 */
typedef enum {
    BUTTON_MODE_MOMENTARY,  /**< Press/release — short/long/very-long detection */
    BUTTON_MODE_LATCHED,    /**< Toggle switch — reports state changes (on/off) */
} button_mode_t;

/**
 * @brief Button event types delivered via callback
 */
typedef enum {
    BUTTON_PRESS_SHORT,      /**< Momentary: released before long threshold */
    BUTTON_PRESS_LONG,       /**< Momentary: held past long threshold */
    BUTTON_PRESS_VERY_LONG,  /**< Momentary: held past very-long threshold */
    BUTTON_LATCH_ON,         /**< Latched: switch moved to active state */
    BUTTON_LATCH_OFF,        /**< Latched: switch moved to inactive state */
} button_press_t;

/**
 * @brief Button event callback signature
 *
 * @param button_id ID returned by button_driver_register()
 * @param press_type Type of button event
 * @param ctx User context from button_config_t
 *
 * @note Called from FreeRTOS timer daemon task. Must not block or call
 *       button_driver_register/unregister (deadlock risk).
 */
typedef void (*button_callback_t)(int button_id, button_press_t press_type, void *ctx);

/**
 * @brief Button registration configuration
 */
typedef struct {
    gpio_num_t gpio;              /**< GPIO pin number */
    bool active_low;              /**< true: press = LOW (internal pull-up enabled) */
    button_mode_t mode;           /**< MOMENTARY or LATCHED */
    uint16_t debounce_ms;         /**< Debounce period. 0 = Kconfig default */
    uint16_t long_press_ms;       /**< Long press threshold. 0 = Kconfig default */
    uint16_t very_long_press_ms;  /**< Very long press threshold. 0 = Kconfig default, 0xFFFF = disabled */
    button_callback_t callback;   /**< Event callback (required) */
    void *ctx;                    /**< User context passed to callback */
} button_config_t;

/**
 * @brief Initialize the button driver
 *
 * Installs GPIO ISR service and creates internal resources.
 * Must be called before register/unregister.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if already initialized
 * @return ESP_ERR_NO_MEM if resource allocation fails
 */
esp_err_t button_driver_init(void);

/**
 * @brief Deinitialize the button driver
 *
 * Unregisters all buttons and releases resources.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t button_driver_deinit(void);

/**
 * @brief Register a button
 *
 * Configures GPIO, installs ISR handler, creates debounce timer.
 *
 * @param config Button configuration (callback is required)
 * @return Non-negative button_id on success (0 to MAX_BUTTONS-1)
 * @return -1 on error (no slots, invalid config, GPIO config failed)
 */
int button_driver_register(const button_config_t *config);

/**
 * @brief Unregister a button
 *
 * Removes ISR handler, deletes timer, releases slot.
 *
 * @param button_id ID returned by button_driver_register()
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if button_id is invalid or not registered
 */
esp_err_t button_driver_unregister(int button_id);

#ifdef __cplusplus
}
#endif

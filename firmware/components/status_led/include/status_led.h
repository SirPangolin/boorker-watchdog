/**
 * @file status_led.h
 * @brief Status LED component (event bus channel subscriber)
 *
 * Maps system states to LED patterns via onboard (and optional external) LED.
 * Subscribes to event_bus for state notifications and renders appropriate
 * LED patterns based on the current event state.
 *
 * @note Thread Safety: All public functions are thread-safe and may be
 *       called from multiple tasks.
 *
 * @note Event Bus Integration: State changes come from event_bus callbacks.
 *       Domains should use event_bus_set_state()/event_bus_clear_state() rather
 *       than calling status_led directly.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Internal LED pattern states (used for led_indicator pattern indexing)
 *
 * @note These are internal pattern identifiers, not a public API. Event states
 *       are mapped to these patterns in the event bus callback handler.
 *
 * @note Enum values use STATUS_LED_ prefix to avoid conflict with led_indicator's
 *       LED_STATE_OFF/LED_STATE_ON brightness values.
 */
typedef enum {
    // Pattern identifiers for LED visual feedback
    // Note: Priority is handled by event bus, not by this enum's ordering.
    // These simply define the visual patterns to display.

    STATUS_LED_ALERT_CRITICAL,       ///< Fast double-pulse (red on RGB)
    STATUS_LED_FIRST_BOOT,           ///< Slow breathe (purple on RGB)
    STATUS_LED_WIFI_PROVISIONING,    ///< Slow blink (blue on RGB)
    STATUS_LED_WIFI_RECONNECTING,    ///< Medium blink (yellow on RGB)
    STATUS_LED_ALERT_ACTIVE,         ///< Double-pulse slow (orange on RGB)
    STATUS_LED_WIFI_CONNECTING,      ///< Fast blink (cyan on RGB)
    STATUS_LED_TAILSCALE_CONNECTING, ///< Fast blink (cyan on RGB)
    STATUS_LED_CONNECTED,            ///< Solid on (green on RGB)
    STATUS_LED_OFF,                  ///< Off

    STATUS_LED_MAX                   ///< Sentinel for bounds checking
} status_led_state_t;

/**
 * @brief Initialize status LED component
 *
 * Creates internal mutex, loads config from NVS (or uses Kconfig defaults),
 * initializes the LED indicator driver(s), and registers as an event bus channel.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if already initialized
 * @return ESP_ERR_NO_MEM if mutex creation fails
 * @return ESP_FAIL if LED indicator creation fails
 */
esp_err_t status_led_init(void);

/**
 * @brief Deinitialize status LED component
 *
 * Stops any active patterns, destroys LED indicator(s), and frees resources.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex acquisition fails
 */
esp_err_t status_led_deinit(void);

/**
 * @brief Get currently displayed state
 *
 * @return Current pattern state, or STATUS_LED_OFF if not initialized
 */
status_led_state_t status_led_get_state(void);

/**
 * @brief Enable or disable status LED
 *
 * When disabled, LED is turned off regardless of state.
 * State tracking continues so re-enabling shows correct state.
 *
 * @param enabled true to enable, false to disable
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex acquisition fails
 */
esp_err_t status_led_set_enabled(bool enabled);

/**
 * @brief Set LED brightness
 *
 * Only effective for LEDC and RGB LED types.
 *
 * @param percent Brightness 0-100
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if percent > 100
 */
esp_err_t status_led_set_brightness(uint8_t percent);

/**
 * @brief Set alerts-only mode
 *
 * When enabled, only alert states (ALERT_CRITICAL, ALERT_ACTIVE) are shown.
 * Other states are tracked but LED stays off.
 *
 * @param alerts_only true for alerts only, false for all states
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex acquisition fails
 */
esp_err_t status_led_set_alerts_only(bool alerts_only);

/**
 * @brief Save current config to NVS
 *
 * Persists enabled, brightness, and alerts_only settings.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex acquisition fails
 * @return ESP_ERR_NVS_* on NVS errors
 */
esp_err_t status_led_save_config(void);

/**
 * @brief Check if status LED is enabled
 *
 * @return true if enabled
 */
bool status_led_is_enabled(void);

/**
 * @brief Get current brightness setting
 *
 * @return Brightness 0-100
 */
uint8_t status_led_get_brightness(void);

/**
 * @brief Check if alerts-only mode is active
 *
 * @return true if alerts-only mode
 */
bool status_led_is_alerts_only(void);

/**
 * @brief Register console commands
 *
 * Registers: led, led_on, led_off, led_brightness, led_alerts, led_all
 *
 * @return ESP_OK on success
 */
esp_err_t status_led_register_console(void);

/**
 * @brief Get state name string for logging
 *
 * @param state State to get name for
 * @return State name string (e.g., "CONNECTED", "PROVISIONING")
 */
const char *status_led_state_name(status_led_state_t state);

#ifdef __cplusplus
}
#endif

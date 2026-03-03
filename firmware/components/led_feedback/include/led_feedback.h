/**
 * @file led_feedback.h
 * @brief LED status feedback component (ANDON channel subscriber)
 *
 * Provides visual status indication via onboard (and optional external) LED.
 * Subscribes to andon_service for state notifications and renders appropriate
 * LED patterns based on the current ANDON state.
 *
 * @note Thread Safety: All public functions are thread-safe and may be
 *       called from multiple tasks.
 *
 * @note ANDON Integration: State changes come from andon_service callbacks.
 *       Domains should use andon_set_state()/andon_clear_state() rather than
 *       calling led_feedback directly.
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
 * @note These are internal pattern identifiers, not a public API. ANDON states
 *       are mapped to these patterns in the ANDON callback handler.
 *
 * @note Enum values use LED_FB_ prefix to avoid conflict with led_indicator's
 *       LED_STATE_OFF/LED_STATE_ON brightness values.
 */
typedef enum {
    // Pattern identifiers for LED visual feedback
    // Note: Priority is handled by ANDON service, not by this enum's ordering.
    // These simply define the visual patterns to display.

    LED_FB_ALERT_CRITICAL,       ///< Fast double-pulse (red on RGB)
    LED_FB_FIRST_BOOT,           ///< Slow breathe (purple on RGB)
    LED_FB_WIFI_PROVISIONING,    ///< Slow blink (blue on RGB)
    LED_FB_WIFI_RECONNECTING,    ///< Medium blink (yellow on RGB)
    LED_FB_ALERT_ACTIVE,         ///< Double-pulse slow (orange on RGB)
    LED_FB_WIFI_CONNECTING,      ///< Fast blink (cyan on RGB)
    LED_FB_TAILSCALE_CONNECTING, ///< Fast blink (cyan on RGB)
    LED_FB_CONNECTED,            ///< Solid on (green on RGB)
    LED_FB_OFF,                  ///< Off

    LED_FB_MAX                   ///< Sentinel for bounds checking
} led_state_t;

/**
 * @brief Initialize LED feedback component
 *
 * Creates internal mutex, loads config from NVS (or uses Kconfig defaults),
 * initializes the LED indicator driver(s), and registers as an ANDON channel.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if already initialized
 * @return ESP_ERR_NO_MEM if mutex creation fails
 * @return ESP_FAIL if LED indicator creation fails
 */
esp_err_t led_feedback_init(void);

/**
 * @brief Deinitialize LED feedback component
 *
 * Stops any active patterns, destroys LED indicator(s), and frees resources.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex acquisition fails
 */
esp_err_t led_feedback_deinit(void);

/**
 * @brief Get currently displayed state
 *
 * @return Current pattern state, or LED_FB_OFF if not initialized
 */
led_state_t led_feedback_get_state(void);

/**
 * @brief Enable or disable LED feedback
 *
 * When disabled, LED is turned off regardless of state.
 * State tracking continues so re-enabling shows correct state.
 *
 * @param enabled true to enable, false to disable
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex acquisition fails
 */
esp_err_t led_feedback_set_enabled(bool enabled);

/**
 * @brief Set LED brightness
 *
 * Only effective for LEDC and RGB LED types.
 *
 * @param percent Brightness 0-100
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if percent > 100
 */
esp_err_t led_feedback_set_brightness(uint8_t percent);

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
esp_err_t led_feedback_set_alerts_only(bool alerts_only);

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
esp_err_t led_feedback_save_config(void);

/**
 * @brief Check if LED feedback is enabled
 *
 * @return true if enabled
 */
bool led_feedback_is_enabled(void);

/**
 * @brief Get current brightness setting
 *
 * @return Brightness 0-100
 */
uint8_t led_feedback_get_brightness(void);

/**
 * @brief Check if alerts-only mode is active
 *
 * @return true if alerts-only mode
 */
bool led_feedback_is_alerts_only(void);

/**
 * @brief Register console commands
 *
 * Registers: led, led_on, led_off, led_brightness, led_alerts, led_all
 *
 * @return ESP_OK on success
 */
esp_err_t led_feedback_register_console(void);

/**
 * @brief Get state name string for logging
 *
 * @param state State to get name for
 * @return State name string (e.g., "CONNECTED", "PROVISIONING")
 */
const char *led_feedback_state_name(led_state_t state);

#ifdef __cplusplus
}
#endif

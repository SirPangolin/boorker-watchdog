/**
 * @file led_feedback.h
 * @brief LED status feedback component
 *
 * Provides visual status indication via onboard LED. Uses priority-based
 * state management - higher priority states preempt lower priority states.
 *
 * @note Thread Safety: All public functions are thread-safe and may be
 *       called from multiple tasks (e.g., WiFi callbacks, main task).
 *       Internal mutex protects state transitions.
 *
 * @note Priority: States are prioritized by enum order. Lower enum value
 *       = higher priority. Use clear_state() to allow lower priority
 *       states to resume.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED feedback states (priority order - lower value = higher priority)
 *
 * @note Enum values use LED_FB_ prefix to avoid conflict with led_indicator's
 *       LED_STATE_OFF/LED_STATE_ON brightness values.
 */
typedef enum {
    // Priority 0 (highest) - Immediate attention
    LED_FB_ALERT_CRITICAL,       ///< Fast double-pulse (red on RGB)

    // Priority 1 - Setup required
    LED_FB_FIRST_BOOT,           ///< Slow breathe (purple on RGB)
    LED_FB_WIFI_PROVISIONING,    ///< Slow blink (blue on RGB)

    // Priority 2 - Problems
    LED_FB_WIFI_RECONNECTING,    ///< Medium blink (yellow on RGB)
    LED_FB_ALERT_ACTIVE,         ///< Double-pulse slow (orange on RGB)

    // Priority 3 - Transitional
    LED_FB_WIFI_CONNECTING,      ///< Fast blink (cyan on RGB)
    LED_FB_TAILSCALE_CONNECTING, ///< Fast blink (cyan on RGB)

    // Priority 4 (lowest) - Normal operation
    LED_FB_CONNECTED,            ///< Solid on (green on RGB)
    LED_FB_OFF,                  ///< Off

    LED_FB_MAX                   ///< Sentinel for bounds checking
} led_state_t;

/**
 * @brief Initialize LED feedback component
 *
 * Creates internal mutex, loads config from NVS (or uses Kconfig defaults),
 * and initializes the LED indicator driver.
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
 * Stops any active patterns, destroys LED indicator, and frees resources.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t led_feedback_deinit(void);

/**
 * @brief Set LED state (activates pattern)
 *
 * If this state has higher priority than current, it takes over immediately.
 * If lower priority, it becomes pending and activates when higher clears.
 *
 * @param state State to activate
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if state >= LED_FB_MAX
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex acquisition fails
 */
esp_err_t led_feedback_set_state(led_state_t state);

/**
 * @brief Clear LED state (deactivates pattern)
 *
 * Removes this state from active set. If it was showing, the next highest
 * priority active state takes over.
 *
 * @param state State to clear
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if state >= LED_FB_MAX
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex acquisition fails
 */
esp_err_t led_feedback_clear_state(led_state_t state);

/**
 * @brief Get currently displayed state
 *
 * @return Current state, or LED_FB_OFF if not initialized
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
 */
esp_err_t led_feedback_set_alerts_only(bool alerts_only);

/**
 * @brief Save current config to NVS
 *
 * Persists enabled, brightness, and alerts_only settings.
 *
 * @return ESP_OK on success
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
 * Registers: led status, led on, led off, led brightness <n>, led alerts-only
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

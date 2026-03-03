/**
 * @file andon_service.h
 * @brief ANDON notification hub service
 *
 * Implements a centralized notification hub inspired by Toyota Production System's
 * ANDON concept. Domains (wifi_manager, rules_engine, etc.) publish states via
 * andon_set_state(). Channels (led_feedback, buzzer, etc.) subscribe via
 * andon_register_channel() and receive callbacks when the active state changes.
 *
 * States are tracked via a bitmask, allowing multiple states to be active
 * simultaneously. The highest priority state (lowest enum value) is returned
 * as the active state.
 *
 * Business states (ALERT_CRITICAL, ALERT_ACTIVE, SENSOR_WARNING) are gated
 * by device claim status - they are blocked when the device is unclaimed.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief ANDON states in priority order (lower value = higher priority)
 *
 * System states: Always processed regardless of device claim status.
 * Business states: Gated when device is unclaimed (andon_set_state returns error).
 *
 * Use andon_is_business_state() to check if a state is business or system.
 * Do NOT rely on enum values - the boundary may change as states are added.
 */
typedef enum {
    // System states (always active) - priority order
    ANDON_FIRST_BOOT = 0,           /**< First boot, device unclaimed (highest when unclaimed) */
    ANDON_ERROR,                    /**< System error condition */
    ANDON_WIFI_PROVISIONING,        /**< WiFi provisioning mode active */
    ANDON_WIFI_RECONNECTING,        /**< WiFi connection lost, reconnecting */
    ANDON_WIFI_CONNECTING,          /**< WiFi connecting (initial) */
    ANDON_TAILSCALE_CONNECTING,     /**< Tailscale VPN connecting */
    ANDON_CONNECTED,                /**< All systems connected and operational */
    ANDON_OFF,                      /**< No active notification (lowest system priority) */

    // --- Business state boundary (gated when unclaimed) ---
    ANDON_ALERT_CRITICAL,           /**< Critical alert requiring immediate attention */
    ANDON_ALERT_ACTIVE,             /**< Active alert condition */
    ANDON_SENSOR_WARNING,           /**< Sensor warning threshold exceeded */

    ANDON_MAX                       /**< Number of states (must be last) */
} andon_state_t;

/**
 * @brief Channel callback function signature
 *
 * Called when the active ANDON state changes. Channels should update their
 * output (LED pattern, buzzer tone, etc.) based on the new state.
 *
 * @note Initial callback during registration is called WITHOUT ANDON mutex held.
 *       Subsequent state change callbacks MAY be called with ANDON mutex held.
 *       Callbacks MUST NOT call back into ANDON service (deadlock risk).
 *       Callbacks should be fast and non-blocking.
 *
 * @param new_state The new active ANDON state
 * @param ctx User context pointer passed during registration
 */
typedef void (*andon_channel_cb_t)(andon_state_t new_state, void *ctx);

/**
 * @brief Initialize ANDON service
 *
 * Creates mutex and initializes state tracking. Must be called before other
 * ANDON functions. Safe to call multiple times (returns ESP_OK if already
 * initialized).
 *
 * @return ESP_OK on success
 * @return ESP_ERR_NO_MEM if mutex creation fails
 */
esp_err_t andon_init(void);

/**
 * @brief Deinitialize ANDON service
 *
 * Releases resources and clears all registered channels.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex cannot be acquired
 */
esp_err_t andon_deinit(void);

/**
 * @brief Set (activate) an ANDON state
 *
 * Adds the state to the active states bitmask. If this changes the highest
 * priority active state, all registered channels are notified.
 *
 * Business states are blocked when device is unclaimed.
 *
 * @param state State to activate
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if state is invalid
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_NOT_ALLOWED if business state and device unclaimed
 * @return ESP_ERR_TIMEOUT if mutex cannot be acquired
 */
esp_err_t andon_set_state(andon_state_t state);

/**
 * @brief Clear (deactivate) an ANDON state
 *
 * Removes the state from the active states bitmask. If this changes the
 * highest priority active state, all registered channels are notified.
 *
 * @param state State to deactivate
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if state is invalid
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex cannot be acquired
 */
esp_err_t andon_clear_state(andon_state_t state);

/**
 * @brief Get current highest priority active state
 *
 * Returns the lowest enum value among all currently active states.
 *
 * @return Current active state, or ANDON_OFF if none active or not initialized
 */
andon_state_t andon_get_active_state(void);

/**
 * @brief Register a notification channel
 *
 * Registers a callback to be notified when the active ANDON state changes.
 * The callback is immediately called with the current active state.
 *
 * @param name Human-readable channel name for logging (e.g., "led_feedback")
 * @param cb Callback function to invoke on state changes
 * @param ctx User context pointer passed to callback
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if name or cb is NULL
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_NO_MEM if max channels reached
 * @return ESP_ERR_TIMEOUT if mutex cannot be acquired
 */
esp_err_t andon_register_channel(const char *name, andon_channel_cb_t cb, void *ctx);

/**
 * @brief Check if a state is a business state
 *
 * Business states (ALERT_CRITICAL, ALERT_ACTIVE, SENSOR_WARNING) are gated
 * by device claim status.
 *
 * @param state State to check
 * @return true if state is a business state
 * @return false if state is a system state or invalid
 */
bool andon_is_business_state(andon_state_t state);

/**
 * @brief Get string name for ANDON state
 *
 * @param state ANDON state
 * @return Human-readable state name, or "UNKNOWN" if invalid
 */
const char *andon_state_name(andon_state_t state);

#ifdef __cplusplus
}
#endif

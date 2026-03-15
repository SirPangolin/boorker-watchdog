/**
 * @file event_bus.h
 * @brief Event bus notification hub
 *
 * Implements a centralized pub/sub notification hub. Domains (wifi_manager,
 * rules_engine, etc.) publish states via event_bus_set_state(). Channels
 * (status_led, buzzer, etc.) subscribe via event_bus_register_channel() and
 * receive callbacks when the active state changes.
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
 * @brief Event states in priority order (lower value = higher priority)
 *
 * System states: Always processed regardless of device claim status.
 * Business states: Gated when device is unclaimed (event_bus_set_state returns error).
 *
 * Use event_bus_is_business_state() to check if a state is business or system.
 * Do NOT rely on enum values - the boundary may change as states are added.
 */
typedef enum {
    // System states (always active) - priority order
    EVENT_FIRST_BOOT = 0,           /**< First boot, device unclaimed (highest when unclaimed) */
    EVENT_ERROR,                    /**< System error condition */
    EVENT_WIFI_PROVISIONING,        /**< WiFi provisioning mode active */
    EVENT_WIFI_RECONNECTING,        /**< WiFi connection lost, reconnecting */
    EVENT_WIFI_CONNECTING,          /**< WiFi connecting (initial) */
    EVENT_TAILSCALE_CONNECTING,     /**< Tailscale VPN connecting */
    EVENT_CONNECTED,                /**< All systems connected and operational */
    EVENT_OFF,                      /**< No active notification (lowest system priority) */

    // --- Business state boundary (gated when unclaimed) ---
    EVENT_ALERT_CRITICAL,           /**< Critical alert requiring immediate attention */
    EVENT_ALERT_ACTIVE,             /**< Active alert condition */
    EVENT_SENSOR_WARNING,           /**< Sensor warning threshold exceeded */

    EVENT_MAX                       /**< Number of states (must be last) */
} event_state_t;

/**
 * @brief MOTD priority levels
 */
typedef enum {
    MOTD_PRIORITY_INFO = 0,      /**< Informational (lowest) */
    MOTD_PRIORITY_WARNING,       /**< Warning */
    MOTD_PRIORITY_CRITICAL,      /**< Critical (highest) */
    MOTD_PRIORITY_MAX            /**< Sentinel — must be last */
} motd_priority_t;

/**
 * @brief MOTD entry structure
 */
typedef struct {
    uint32_t id;                 /**< Unique ID for dismissal */
    char source[16];             /**< Source component (e.g., "ota") */
    char message[128];           /**< MOTD message text */
    motd_priority_t priority;    /**< Priority level */
    uint32_t timestamp;          /**< Seconds since boot (esp_log_timestamp / 1000) */
} motd_entry_t;

#define EVENT_BUS_MAX_MOTDS 4    /**< Maximum concurrent MOTDs */

/**
 * @brief Channel callback function signature
 *
 * Called when the active event state changes. Channels should update their
 * output (LED pattern, buzzer tone, etc.) based on the new state.
 *
 * @note Initial callback during registration is called WITHOUT event_bus mutex held.
 *       Subsequent state change callbacks MAY be called with event_bus mutex held.
 *       Callbacks MUST NOT call back into event_bus (deadlock risk).
 *       Callbacks should be fast and non-blocking.
 *
 * @param new_state The new active event state
 * @param ctx User context pointer passed during registration
 */
typedef void (*event_channel_cb_t)(event_state_t new_state, void *ctx);

/**
 * @brief Initialize event bus
 *
 * Creates mutex and initializes state tracking. Must be called before other
 * event_bus functions. Safe to call multiple times (returns ESP_OK if already
 * initialized).
 *
 * @return ESP_OK on success
 * @return ESP_ERR_NO_MEM if mutex creation fails
 */
esp_err_t event_bus_init(void);

/**
 * @brief Deinitialize event bus
 *
 * Releases resources and clears all registered channels.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex cannot be acquired
 */
esp_err_t event_bus_deinit(void);

/**
 * @brief Set (activate) an event state
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
esp_err_t event_bus_set_state(event_state_t state);

/**
 * @brief Clear (deactivate) an event state
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
esp_err_t event_bus_clear_state(event_state_t state);

/**
 * @brief Get current highest priority active state
 *
 * Returns the lowest enum value among all currently active states.
 *
 * @return Current active state, or EVENT_OFF if none active or not initialized
 */
event_state_t event_bus_get_active_state(void);

/**
 * @brief Register a notification channel
 *
 * Registers a callback to be notified when the active event state changes.
 * The callback is immediately called with the current active state.
 *
 * @param name Human-readable channel name for logging (e.g., "status_led")
 * @param cb Callback function to invoke on state changes
 * @param ctx User context pointer passed to callback
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if name or cb is NULL
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_NO_MEM if max channels reached
 * @return ESP_ERR_TIMEOUT if mutex cannot be acquired
 */
esp_err_t event_bus_register_channel(const char *name, event_channel_cb_t cb, void *ctx);

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
bool event_bus_is_business_state(event_state_t state);

/**
 * @brief Get string name for event state
 *
 * @param state Event state
 * @return Human-readable state name, or "UNKNOWN" if invalid
 */
const char *event_state_name(event_state_t state);

/**
 * @brief Post an MOTD notification
 *
 * @param source Component name posting the MOTD (max 15 chars)
 * @param message MOTD message text (max 127 chars)
 * @param priority Priority level
 * @return ESP_OK on success
 * @return ESP_ERR_NO_MEM if MOTD slots full
 * @return ESP_ERR_INVALID_ARG if source or message is NULL
 */
esp_err_t event_bus_post_motd(const char *source, const char *message, motd_priority_t priority);

/**
 * @brief Copy active MOTDs into caller-provided buffer
 *
 * Thread-safe: copies MOTD data under lock so the caller owns the result.
 *
 * @param out       Destination buffer for MOTD entries
 * @param max_count Capacity of @p out (number of entries)
 * @param count     Output: number of entries actually copied
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if out/count is NULL
 */
esp_err_t event_bus_get_motds(motd_entry_t *out, size_t max_count, size_t *count);

/**
 * @brief Dismiss an MOTD by ID
 *
 * @param id MOTD ID to dismiss
 * @return ESP_OK on success
 * @return ESP_ERR_NOT_FOUND if ID not found
 */
esp_err_t event_bus_dismiss_motd(uint32_t id);

/**
 * @brief Clear all MOTDs from a source
 *
 * @param source Source component name
 * @return ESP_OK on success
 */
esp_err_t event_bus_clear_motds_from(const char *source);

#ifdef __cplusplus
}
#endif

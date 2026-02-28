#pragma once

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Tailscale manager event types
 */
typedef enum {
    TS_MGR_EVENT_CONNECTED,           ///< Got Tailscale IP
    TS_MGR_EVENT_DISCONNECTED,        ///< Lost connection, will retry
    TS_MGR_EVENT_UNCONFIGURED,        ///< No auth key in NVS
    TS_MGR_EVENT_RECONNECT_EXHAUSTED, ///< Max reconnection attempts reached
    TS_MGR_EVENT_KEY_UPDATED,         ///< New auth key set
} ts_mgr_event_t;

/**
 * @brief Callback function signature for Tailscale events
 */
typedef void (*ts_mgr_callback_t)(ts_mgr_event_t event, void *ctx);

/**
 * @brief Tailscale manager configuration
 */
typedef struct {
    const char *device_name;       ///< Tailscale device name (NULL = use Kconfig)
    ts_mgr_callback_t callback;    ///< Event callback (optional)
    void *callback_ctx;            ///< Context passed to callback
} ts_mgr_config_t;

/**
 * @brief Initialize Tailscale manager
 *
 * Checks NVS for stored auth key. If found, begins connection.
 * If not found, fires TS_MGR_EVENT_UNCONFIGURED.
 *
 * Should be called after WiFi is connected for immediate connection attempts.
 * If WiFi isn't ready, connection will fail but can be retried when WiFi connects.
 *
 * @note Call from main task or task with adequate stack (>8KB) - MicroLink
 *       performs heavy initialization. Do NOT call from event callbacks.
 *
 * @param config Configuration (can be NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t ts_mgr_init(const ts_mgr_config_t *config);

/**
 * @brief Stop Tailscale manager and cleanup
 * @return ESP_OK on success
 */
esp_err_t ts_mgr_stop(void);

/**
 * @brief Check if connected to Tailscale
 * @return true if connected with Tailscale IP
 */
bool ts_mgr_is_connected(void);

/**
 * @brief Check if auth key is configured
 * @return true if auth key exists in NVS
 */
bool ts_mgr_is_configured(void);

/**
 * @brief Get current state name for logging
 * @return State name string
 */
const char* ts_mgr_get_state_name(void);

/**
 * @brief Get Tailscale VPN IP address as string
 * @param buf Buffer to write IP string (minimum 16 bytes)
 * @param len Buffer length
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not connected
 */
esp_err_t ts_mgr_get_ip(char *buf, size_t len);

/**
 * @brief Set Tailscale auth key (stores in NVS)
 *
 * Validates key format, stores in NVS, and triggers connection.
 * Key is never logged for security.
 *
 * @param key Auth key (must start with "tskey-auth-")
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if format invalid
 */
esp_err_t ts_mgr_set_auth_key(const char *key);

/**
 * @brief Clear stored auth key
 * @return ESP_OK on success
 */
esp_err_t ts_mgr_clear_auth_key(void);

/**
 * @brief Check if auth key exists in NVS
 * @return true if key is stored
 */
bool ts_mgr_has_auth_key(void);

/**
 * @brief Register console commands (ts_auth, ts_clear, ts_status)
 *
 * Call after esp_console is initialized.
 *
 * @return ESP_OK on success
 */
esp_err_t ts_console_register(void);

#ifdef __cplusplus
}
#endif

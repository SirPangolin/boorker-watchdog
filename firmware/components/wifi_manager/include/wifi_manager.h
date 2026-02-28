#pragma once

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Event bits for synchronization
#define WIFI_MGR_CONNECTED_BIT    BIT0
#define WIFI_MGR_DISCONNECTED_BIT BIT1
#define WIFI_MGR_PROVISIONED_BIT  BIT2

// Event types for callbacks
typedef enum {
    WIFI_MGR_EVENT_CONNECTED,      // Got IP address
    WIFI_MGR_EVENT_DISCONNECTED,   // Lost connection
    WIFI_MGR_EVENT_PROVISIONING,   // Entered provisioning mode
    WIFI_MGR_EVENT_PROVISIONED,    // Credentials received via BLE
} wifi_mgr_event_t;

// Callback signature
typedef void (*wifi_mgr_callback_t)(wifi_mgr_event_t event, void *ctx);

// Configuration
typedef struct {
    const char *device_name;       // For mDNS and BLE provisioning (NULL = use Kconfig)
    bool start_provisioning;       // Force provisioning even if creds exist
    wifi_mgr_callback_t callback;  // Event callback (optional)
    void *callback_ctx;            // Context passed to callback
} wifi_mgr_config_t;

/**
 * @brief Initialize and start WiFi manager
 *
 * This function initializes WiFi, checks for stored credentials,
 * and either connects to the saved network or starts BLE provisioning.
 *
 * @param config Configuration struct (can be NULL for defaults)
 * @return ESP_OK on success
 */
esp_err_t wifi_mgr_init(const wifi_mgr_config_t *config);

/**
 * @brief Check if WiFi is connected
 * @return true if connected with IP address
 */
bool wifi_mgr_is_connected(void);

/**
 * @brief Get current IP address
 * @param buf Buffer to write IP string
 * @param len Buffer length (minimum 16 bytes for IPv4)
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if not connected
 */
esp_err_t wifi_mgr_get_ip(char *buf, size_t len);

/**
 * @brief Get current state name for logging
 * @return State name string (e.g., "CONNECTED", "PROVISIONING")
 */
const char* wifi_mgr_get_state_name(void);

/**
 * @brief Get event group for task synchronization
 * @return FreeRTOS event group handle
 */
EventGroupHandle_t wifi_mgr_get_event_group(void);

/**
 * @brief Force start BLE provisioning
 *
 * Disconnects from current network (if any) and starts BLE provisioning.
 * Useful for re-provisioning to a different network.
 *
 * @return ESP_OK on success
 */
esp_err_t wifi_mgr_start_provisioning(void);

/**
 * @brief Clear stored WiFi credentials
 * @return ESP_OK on success
 */
esp_err_t wifi_mgr_clear_credentials(void);

/**
 * @brief Stop WiFi manager and cleanup
 * @return ESP_OK on success
 */
esp_err_t wifi_mgr_stop(void);

#ifdef __cplusplus
}
#endif

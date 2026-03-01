#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the web server
 * - Mounts LittleFS
 * - Starts HTTP server on configured port
 * - Registers all API endpoints
 *
 * @note Requires web_auth and device_identity to be initialized first
 *
 * @return ESP_OK on success
 */
esp_err_t web_server_start(void);

/**
 * Stop the web server
 */
esp_err_t web_server_stop(void);

/**
 * Check if server is running
 */
bool web_server_is_running(void);

#ifdef __cplusplus
}
#endif

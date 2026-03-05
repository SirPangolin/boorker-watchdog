#pragma once

#include "esp_err.h"
#include "esp_http_server.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start the HTTP server
 * - Mounts LittleFS
 * - Starts HTTP server on configured port
 * - Registers all API endpoints
 *
 * @note Requires web_auth and credentials to be initialized first
 *
 * @return ESP_OK on success
 */
esp_err_t http_server_start(void);

/**
 * Stop the HTTP server
 */
esp_err_t http_server_stop(void);

/**
 * Check if server is running
 */
bool http_server_is_running(void);

#ifdef __cplusplus
}
#endif

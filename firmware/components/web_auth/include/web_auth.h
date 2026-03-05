#pragma once

#include "esp_err.h"
#include "esp_http_server.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WEB_AUTH_SESSION_TOKEN_LEN 32
#define WEB_AUTH_MAX_SESSIONS 4

/**
 * Initialize web authentication
 * Uses credentials for default password
 *
 * @note Thread Safety: Session operations are protected by internal mutex
 *       and safe to call from multiple HTTP handler tasks concurrently.
 *       Password operations modify shared state and should be serialized.
 *
 * @return ESP_OK on success
 */
esp_err_t web_auth_init(void);

/**
 * Check if password has been changed from default
 */
bool web_auth_password_changed(void);

/**
 * Validate username/password and create session
 *
 * @param username Username (currently only "admin" supported)
 * @param password Password to validate
 * @param token_out Buffer for session token (WEB_AUTH_SESSION_TOKEN_LEN + 1)
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG on bad credentials
 */
esp_err_t web_auth_login(const char *username, const char *password, char *token_out);

/**
 * Validate session token
 */
bool web_auth_validate_session(const char *token);

/**
 * Invalidate session token (logout)
 *
 * @param token Session token to invalidate
 * @return true if session was found and invalidated, false otherwise
 */
bool web_auth_logout(const char *token);

/**
 * Change password
 *
 * @param current_password Current password for verification
 * @param new_password New password to set
 * @return ESP_OK on success
 */
esp_err_t web_auth_change_password(const char *current_password, const char *new_password);

/**
 * Invalidate all active sessions (logout all devices)
 */
void web_auth_invalidate_all_sessions(void);

/**
 * Check if request is authenticated (session cookie)
 */
bool web_auth_check_request(httpd_req_t *req);

/**
 * HTTP middleware to require authentication
 * Returns 401 if not authenticated
 */
esp_err_t web_auth_require(httpd_req_t *req);

/**
 * Get remaining failed login attempts before lockout
 */
int web_auth_get_attempts_remaining(void);

/**
 * Reset password to device default (factory reset)
 */
esp_err_t web_auth_reset_password(void);

#ifdef __cplusplus
}
#endif

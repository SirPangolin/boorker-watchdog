#include "web_server.h"
#include "web_auth.h"
#include "device_identity.h"
#include "device_state.h"
#include "system_console.h"
#include "version.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_littlefs.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "cJSON.h"
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>

static const char *TAG = "web_server";

// LittleFS mount point for static web content
#define WEB_FS_BASE_PATH "/littlefs"

static httpd_handle_t s_server = NULL;

// MIME type mapping
static const char* get_mime_type(const char *path)
{
    if (strstr(path, ".html")) return "text/html";
    if (strstr(path, ".css")) return "text/css";
    if (strstr(path, ".js")) return "application/javascript";
    if (strstr(path, ".json")) return "application/json";
    if (strstr(path, ".png")) return "image/png";
    if (strstr(path, ".ico")) return "image/x-icon";
    return "application/octet-stream";
}

// Case-insensitive substring search for URL-encoded path traversal
static bool contains_encoded_dot(const char *uri, const char *pattern)
{
    size_t pattern_len = strlen(pattern);
    for (const char *p = uri; *p; p++) {
        bool match = true;
        for (size_t i = 0; i < pattern_len && match; i++) {
            char c = p[i];
            char pat = pattern[i];
            if (c == '\0') {
                match = false;
            } else if (pat == '%' || pat == '.' || (pat >= '0' && pat <= '9')) {
                // Exact match for %, ., and digits
                match = (c == pat);
            } else {
                // Case-insensitive for hex letters (e, E)
                match = (tolower((unsigned char)c) == tolower((unsigned char)pat));
            }
        }
        if (match) return true;
    }
    return false;
}

// Check for path traversal attempts
static bool is_safe_path(const char *uri)
{
    // Reject path traversal attempts - literal ".."
    if (strstr(uri, "..") != NULL) {
        return false;
    }
    // URL-encoded variants (case insensitive for hex digits)
    // %2e = '.', check: %2e%2e, %2e., .%2e (all case combinations)
    if (contains_encoded_dot(uri, "%2e%2e") ||
        contains_encoded_dot(uri, "%2e.") ||
        contains_encoded_dot(uri, ".%2e")) {
        return false;
    }
    return true;
}

// Serve static files from LittleFS with gzip support
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static esp_err_t file_handler(httpd_req_t *req)
{
    // WEB_FS_BASE_PATH + uri + .gz + null = max path
    char filepath[CONFIG_WEB_SERVER_MAX_URI_LEN + sizeof(WEB_FS_BASE_PATH) + 4];
    char uri_buf[CONFIG_WEB_SERVER_MAX_URI_LEN + 1];
    const char *uri;

    // Validate and copy URI to bounded buffer
    size_t uri_len = strlen(req->uri);
    if (uri_len > CONFIG_WEB_SERVER_MAX_URI_LEN) {
        httpd_resp_send_err(req, HTTPD_414_URI_TOO_LONG, "URI too long");
        return ESP_FAIL;
    }
    strncpy(uri_buf, req->uri, sizeof(uri_buf) - 1);
    uri_buf[sizeof(uri_buf) - 1] = '\0';
    uri = uri_buf;

    // Check for path traversal attempts
    if (!is_safe_path(uri)) {
        ESP_LOGW(TAG, "Path traversal attempt blocked: %s", uri);
        httpd_resp_send_err(req, HTTPD_403_FORBIDDEN, "Invalid path");
        return ESP_FAIL;
    }

    // Default to index.html for root
    if (strcmp(uri, "/") == 0) {
        uri = "/index.html";
    }

    // Try gzipped version first
    snprintf(filepath, sizeof(filepath), WEB_FS_BASE_PATH "%s.gz", uri);

    struct stat st;
    bool gzipped = false;

    if (stat(filepath, &st) == 0) {
        gzipped = true;
    } else {
        // Try non-gzipped
        snprintf(filepath, sizeof(filepath), WEB_FS_BASE_PATH "%s", uri);
        if (stat(filepath, &st) != 0) {
            // SPA fallback: serve index.html for unknown paths
            snprintf(filepath, sizeof(filepath), WEB_FS_BASE_PATH "/index.html.gz");
            if (stat(filepath, &st) == 0) {
                gzipped = true;
            } else {
                snprintf(filepath, sizeof(filepath), WEB_FS_BASE_PATH "/index.html");
                if (stat(filepath, &st) != 0) {
                    httpd_resp_send_404(req);
                    return ESP_OK;
                }
            }
        }
    }

    FILE *f = fopen(filepath, "r");
    if (!f) {
        ESP_LOGE(TAG, "Failed to open %s", filepath);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Set content type based on original URI (not .gz version)
    const char *mime = get_mime_type(uri);
    httpd_resp_set_type(req, mime);

    if (gzipped) {
        httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
    }

    // Stream file
    char buf[512];
    size_t read_bytes;
    bool read_error = false;
    while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
            ESP_LOGD(TAG, "Chunk send failed for %s (client disconnected?)", filepath);
            fclose(f);
            return ESP_FAIL;
        }
    }

    // Check if loop ended due to error (not just EOF)
    if (ferror(f)) {
        ESP_LOGE(TAG, "Error reading file %s", filepath);
        read_error = true;
    }
    fclose(f);

    if (read_error) {
        // Can't send 500 after chunks started, just abort
        return ESP_FAIL;
    }

    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}
#pragma GCC diagnostic pop

// POST /api/v1/auth/login
static esp_err_t api_auth_login(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret < 0) {
        // Socket error
        ESP_LOGD(TAG, "Login recv error: %d", ret);
        return ESP_FAIL;  // Don't send response on socket error
    }
    if (ret == 0) {
        // Connection closed or timeout - no body received
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *username = cJSON_GetObjectItem(json, "username");
    cJSON *password = cJSON_GetObjectItem(json, "password");

    if (!cJSON_IsString(username) || !cJSON_IsString(password)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing credentials");
        return ESP_FAIL;
    }

    char token[WEB_AUTH_SESSION_TOKEN_LEN + 1];
    esp_err_t err = web_auth_login(username->valuestring, password->valuestring, token);
    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");

    if (err == ESP_OK) {
        // Set session cookie
        char cookie[128];
        snprintf(cookie, sizeof(cookie), "session=%s; Path=/; HttpOnly; SameSite=Strict", token);
        httpd_resp_set_hdr(req, "Set-Cookie", cookie);

        // Check if password change is required (device unclaimed)
        if (!device_state_is_claimed()) {
            httpd_resp_sendstr(req, "{\"success\":true,\"password_change_required\":true}");
        } else {
            httpd_resp_sendstr(req, "{\"success\":true}");
        }
    } else {
        httpd_resp_set_status(req, "401 Unauthorized");
        int remaining = web_auth_get_attempts_remaining();
        char resp[64];
        snprintf(resp, sizeof(resp), "{\"error\":true,\"attempts_remaining\":%d}", remaining);
        httpd_resp_sendstr(req, resp);
    }

    return ESP_OK;
}

// POST /api/v1/auth/logout
static esp_err_t api_auth_logout(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK; // Auth failed, response already sent
    }

    bool session_invalidated = false;

    // Extract session token from cookie and invalidate server-side
    char cookie_buf[128];
    if (httpd_req_get_hdr_value_str(req, "Cookie", cookie_buf, sizeof(cookie_buf)) == ESP_OK) {
        char *session = strstr(cookie_buf, "session=");
        if (session) {
            session += 8; // Skip "session="
            char *end = strchr(session, ';');
            size_t token_len = end ? (size_t)(end - session) : strlen(session);
            if (token_len > 0 && token_len <= WEB_AUTH_SESSION_TOKEN_LEN) {
                char token[WEB_AUTH_SESSION_TOKEN_LEN + 1];
                strncpy(token, session, token_len);
                token[token_len] = '\0';
                session_invalidated = web_auth_logout(token);
            }
        }
    }

    // Always clear cookie (even if server-side invalidation failed)
    httpd_resp_set_hdr(req, "Set-Cookie", "session=; Path=/; Max-Age=0; SameSite=Strict");
    httpd_resp_set_type(req, "application/json");

    if (session_invalidated) {
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else {
        // Session was already invalid or couldn't be found - still "logout" client
        ESP_LOGW(TAG, "Logout: session not found server-side (may already be expired)");
        httpd_resp_sendstr(req, "{\"success\":true,\"note\":\"session_not_found\"}");
    }
    return ESP_OK;
}

// GET /api/v1/auth/status - no auth required (used to check if password changed)
static esp_err_t api_auth_status(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    bool authenticated = web_auth_check_request(req);
    bool password_changed = web_auth_password_changed();

    char resp[128];
    snprintf(resp, sizeof(resp),
             "{\"authenticated\":%s,\"password_changed\":%s}",
             authenticated ? "true" : "false",
             password_changed ? "true" : "false");

    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

// PUT /api/v1/auth/password
// NOTE: Uses web_auth_check_request() instead of web_auth_require() because
// this endpoint is needed to claim the device (transition from unclaimed).
// The 403 gate must not block password change.
static esp_err_t api_auth_password(httpd_req_t *req)
{
    if (!web_auth_check_request(req)) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_type(req, "application/json");
        httpd_resp_send(req, "{\"error\":\"unauthorized\"}", HTTPD_RESP_USE_STRLEN);
        return ESP_OK;
    }

    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret < 0) {
        ESP_LOGD(TAG, "Password change recv error: %d", ret);
        return ESP_FAIL;  // Don't send response on socket error
    }
    if (ret == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No body");
        return ESP_FAIL;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Invalid JSON");
        return ESP_FAIL;
    }

    cJSON *current = cJSON_GetObjectItem(json, "current_password");
    cJSON *new_pass = cJSON_GetObjectItem(json, "new_password");

    if (!cJSON_IsString(current) || !cJSON_IsString(new_pass)) {
        cJSON_Delete(json);
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing current_password or new_password");
        return ESP_FAIL;
    }

    // Validate new password length
    size_t new_len = strlen(new_pass->valuestring);
    if (new_len < 8 || new_len > 64) {
        cJSON_Delete(json);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Password must be 8-64 characters\"}");
        return ESP_OK;
    }

    esp_err_t err = web_auth_change_password(current->valuestring, new_pass->valuestring);
    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");

    if (err == ESP_OK) {
        // Clear session cookie (user must re-login)
        httpd_resp_set_hdr(req, "Set-Cookie", "session=; Path=/; Max-Age=0; SameSite=Strict");
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else if (err == ESP_ERR_INVALID_ARG) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Current password incorrect\"}");
    } else {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    return ESP_OK;
}

// POST /api/v1/system/reboot - schedule reboot
// DELETE /api/v1/system/reboot - cancel pending reboot
static esp_err_t api_system_reboot(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK; // Auth failed, response already sent
    }

    httpd_resp_set_type(req, "application/json");

    if (req->method == HTTP_DELETE) {
        // Cancel pending reboot
        esp_err_t err = system_reboot_cancel();
        if (err == ESP_OK) {
            httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Reboot cancelled\"}");
        } else {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"No reboot pending\"}");
        }
        return ESP_OK;
    }

    // POST - schedule reboot
    uint32_t delay = 3; // Default delay

    // Check for body with delay
    int content_len = req->content_len;
    if (content_len > 0) {
        if (content_len >= 64) {
            httpd_resp_set_status(req, "400 Bad Request");
            httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Request body too large\"}");
            return ESP_OK;
        }
        char buf[64];
        int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
        if (ret > 0) {
            buf[ret] = '\0';
            cJSON *json = cJSON_Parse(buf);
            if (!json) {
                ESP_LOGW(TAG, "Malformed JSON in reboot request: %s", buf);
                httpd_resp_set_status(req, "400 Bad Request");
                httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Invalid JSON\"}");
                return ESP_OK;
            }
            cJSON *delay_json = cJSON_GetObjectItem(json, "delay");
            if (cJSON_IsNumber(delay_json)) {
                delay = (uint32_t)delay_json->valueint;
            }
            cJSON_Delete(json);
        }
    }

    // Check if reboot already pending
    if (system_reboot_is_pending()) {
        uint32_t remaining = system_reboot_get_remaining();
        char resp[96];
        snprintf(resp, sizeof(resp),
                 "{\"error\":true,\"message\":\"Reboot already pending\",\"remaining\":%lu}",
                 (unsigned long)remaining);
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_sendstr(req, resp);
        return ESP_OK;
    }

    esp_err_t err = system_reboot_schedule(delay);
    if (err == ESP_OK) {
        char resp[64];
        snprintf(resp, sizeof(resp), "{\"success\":true,\"delay\":%lu}", (unsigned long)delay);
        httpd_resp_sendstr(req, resp);
    } else {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    return ESP_OK;
}

// POST /api/v1/system/factory-reset
static esp_err_t api_system_factory_reset(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");

    // Reset web auth password to default
    esp_err_t err = web_auth_reset_password();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to reset password: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Regenerate device identity (new credentials)
    err = device_identity_regenerate();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to regenerate identity: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Invalidate all sessions before reboot
    web_auth_invalidate_all_sessions();

    ESP_LOGI(TAG, "Factory reset complete - scheduling reboot");

    // Clear session cookie
    httpd_resp_set_hdr(req, "Set-Cookie", "session=; Path=/; Max-Age=0; SameSite=Strict");
    httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Factory reset complete, rebooting...\"}");

    // Schedule reboot after response sent
    err = system_reboot_schedule(2);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "CRITICAL: Failed to schedule reboot after factory reset: %s - device in inconsistent state!",
                 esp_err_to_name(err));
        // Response already sent - try direct restart as fallback
        vTaskDelay(pdMS_TO_TICKS(1000));  // Give response time to send
        esp_restart();
    }

    return ESP_OK;
}

// GET /api/v1/system/status
static esp_err_t api_system_status(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK; // Auth failed, response already sent
    }

    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Check all cJSON_Add* return values for NULL (allocation failure)
    bool ok = true;
    ok = ok && cJSON_AddNumberToObject(json, "uptime", esp_timer_get_time() / 1000000);
    ok = ok && cJSON_AddNumberToObject(json, "heap_free", esp_get_free_heap_size());
    ok = ok && cJSON_AddNumberToObject(json, "psram_free", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    const device_identity_t *id = device_identity_get();
    if (id) {
        ok = ok && cJSON_AddStringToObject(json, "node_name", id->node_name);
    }

    // Add power stub (to be implemented with actual power monitoring)
    cJSON *power = cJSON_CreateObject();
    if (power != NULL) {
        ok = ok && cJSON_AddStringToObject(power, "source", "unknown");
        ok = ok && cJSON_AddBoolToObject(power, "ac_present", true);
        ok = ok && cJSON_AddItemToObject(json, "power", power);
    }

    if (!ok) {
        ESP_LOGE(TAG, "Failed to build status JSON - memory allocation failed");
        cJSON_Delete(json);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char *resp = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!resp) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    free(resp);

    return ESP_OK;
}

// GET /api/v1/system/info
static esp_err_t api_system_info(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK;
    }

    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    // Check all cJSON_Add* return values for NULL (allocation failure)
    bool ok = true;
    ok = ok && cJSON_AddStringToObject(json, "version", BOORKER_VERSION_STRING);
    ok = ok && cJSON_AddStringToObject(json, "idf_version", esp_get_idf_version());
    ok = ok && cJSON_AddNumberToObject(json, "chip_revision", chip_info.revision);
    ok = ok && cJSON_AddNumberToObject(json, "cores", chip_info.cores);

    uint8_t mac[6];
    esp_err_t mac_ret = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (mac_ret == ESP_OK) {
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        ok = ok && cJSON_AddStringToObject(json, "mac", mac_str);
    } else {
        ESP_LOGW(TAG, "Failed to read MAC address: %s", esp_err_to_name(mac_ret));
        ok = ok && cJSON_AddStringToObject(json, "mac", "unknown");
    }

    if (!ok) {
        ESP_LOGE(TAG, "Failed to build info JSON - memory allocation failed");
        cJSON_Delete(json);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char *resp = cJSON_PrintUnformatted(json);
    cJSON_Delete(json);

    if (!resp) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    free(resp);

    return ESP_OK;
}

// GET /api/v1/system/qr - returns QR JSON for device setup (first boot only)
static esp_err_t api_system_qr(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");

    // Only expose credentials during first boot period (security measure)
    if (!device_identity_is_first_boot()) {
        ESP_LOGW(TAG, "QR endpoint accessed after first boot - credentials hidden");
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Credentials only available during first boot\"}");
        return ESP_OK;
    }

    char qr_json[256];
    esp_err_t err = device_identity_get_qr_json(qr_json, sizeof(qr_json));

    if (err == ESP_OK) {
        httpd_resp_sendstr(req, qr_json);
    } else {
        ESP_LOGE(TAG, "Failed to get QR JSON: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Mount LittleFS
static esp_err_t mount_littlefs(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = WEB_FS_BASE_PATH,
        .partition_label = "storage",
        .format_if_mount_failed = false,
        .dont_mount = false,
    };

    esp_err_t ret = esp_vfs_littlefs_register(&conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to mount LittleFS: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total = 0, used = 0;
    ret = esp_littlefs_info("storage", &total, &used);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get LittleFS info: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "LittleFS: %d/%d bytes used", (int)used, (int)total);
    }

    return ESP_OK;
}

esp_err_t web_server_start(void)
{
    if (s_server) {
        ESP_LOGW(TAG, "Server already running");
        return ESP_OK;
    }

    // Mount filesystem
    esp_err_t ret = mount_littlefs();
    if (ret != ESP_OK) {
        return ret;
    }

    // Configure server
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_WEB_SERVER_PORT;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 16;
    config.stack_size = 8192;

    ret = httpd_start(&s_server, &config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start server: %s", esp_err_to_name(ret));
        return ret;
    }

    // Track registration failures
    esp_err_t first_error = ESP_OK;

    // Register API endpoints
    httpd_uri_t uri_login = {
        .uri = "/api/v1/auth/login",
        .method = HTTP_POST,
        .handler = api_auth_login,
    };
    ret = httpd_register_uri_handler(s_server, &uri_login);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/v1/auth/login: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    httpd_uri_t uri_logout = {
        .uri = "/api/v1/auth/logout",
        .method = HTTP_POST,
        .handler = api_auth_logout,
    };
    ret = httpd_register_uri_handler(s_server, &uri_logout);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/v1/auth/logout: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    httpd_uri_t uri_auth_status = {
        .uri = "/api/v1/auth/status",
        .method = HTTP_GET,
        .handler = api_auth_status,
    };
    ret = httpd_register_uri_handler(s_server, &uri_auth_status);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/v1/auth/status: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    httpd_uri_t uri_auth_password = {
        .uri = "/api/v1/auth/password",
        .method = HTTP_PUT,
        .handler = api_auth_password,
    };
    ret = httpd_register_uri_handler(s_server, &uri_auth_password);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/v1/auth/password: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    httpd_uri_t uri_status = {
        .uri = "/api/v1/system/status",
        .method = HTTP_GET,
        .handler = api_system_status,
    };
    ret = httpd_register_uri_handler(s_server, &uri_status);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/v1/system/status: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    httpd_uri_t uri_info = {
        .uri = "/api/v1/system/info",
        .method = HTTP_GET,
        .handler = api_system_info,
    };
    ret = httpd_register_uri_handler(s_server, &uri_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/v1/system/info: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    httpd_uri_t uri_qr = {
        .uri = "/api/v1/system/qr",
        .method = HTTP_GET,
        .handler = api_system_qr,
    };
    ret = httpd_register_uri_handler(s_server, &uri_qr);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/v1/system/qr: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    // Reboot endpoint - POST to schedule, DELETE to cancel
    httpd_uri_t uri_reboot_post = {
        .uri = "/api/v1/system/reboot",
        .method = HTTP_POST,
        .handler = api_system_reboot,
    };
    ret = httpd_register_uri_handler(s_server, &uri_reboot_post);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register POST /api/v1/system/reboot: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    httpd_uri_t uri_reboot_delete = {
        .uri = "/api/v1/system/reboot",
        .method = HTTP_DELETE,
        .handler = api_system_reboot,
    };
    ret = httpd_register_uri_handler(s_server, &uri_reboot_delete);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register DELETE /api/v1/system/reboot: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    httpd_uri_t uri_factory_reset = {
        .uri = "/api/v1/system/factory-reset",
        .method = HTTP_POST,
        .handler = api_system_factory_reset,
    };
    ret = httpd_register_uri_handler(s_server, &uri_factory_reset);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register /api/v1/system/factory-reset: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    // File handler (must be last - wildcard)
    httpd_uri_t uri_files = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = file_handler,
    };
    ret = httpd_register_uri_handler(s_server, &uri_files);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register file handler: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    if (first_error != ESP_OK) {
        ESP_LOGW(TAG, "Web server started with errors - some endpoints may be unavailable");
        // Server is running but degraded - return success but log warning
    }

    ESP_LOGI(TAG, "Web server started on port %d", CONFIG_WEB_SERVER_PORT);
    return ESP_OK;  // Server is running, even if some handlers failed
}

esp_err_t web_server_stop(void)
{
    if (!s_server) {
        return ESP_OK;
    }

    esp_err_t ret = httpd_stop(s_server);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "httpd_stop failed: %s", esp_err_to_name(ret));
    }
    s_server = NULL;

    esp_vfs_littlefs_unregister("storage");
    ESP_LOGI(TAG, "Web server stopped");

    return ret;
}

bool web_server_is_running(void)
{
    return s_server != NULL;
}

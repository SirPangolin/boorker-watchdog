#include "http_server.h"
#include "web_auth.h"
#include "secrets.h"
#include "system_state.h"
#include "event_bus.h"
#include "sys_console.h"
#include "version.h"
#include "ota_manager.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_system.h"
#include "esp_partition.h"
#include "nvs_flash.h"
#if CONFIG_HTTP_SERVER_HTTPS_ENABLED
#include "esp_https_server.h"
#endif
#include "cJSON.h"
#include <string.h>
#include <sys/stat.h>
#include <ctype.h>

static const char *TAG = "http_server";

// LittleFS mount point for static web content
#define WEB_FS_BASE_PATH "/littlefs"

// Cookie flags — add Secure when serving over HTTPS
#if CONFIG_HTTP_SERVER_HTTPS_ENABLED
#define COOKIE_CLEAR "session=; Path=/; HttpOnly; Max-Age=0; SameSite=Strict; Secure"
#define COOKIE_SECURE_FLAG "; Secure"
#else
#define COOKIE_CLEAR "session=; Path=/; HttpOnly; Max-Age=0; SameSite=Strict"
#define COOKIE_SECURE_FLAG ""
#endif

static httpd_handle_t s_server = NULL;
#if CONFIG_HTTP_SERVER_HTTPS_ENABLED
static httpd_handle_t s_redirect_server = NULL;
#endif

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
    char filepath[CONFIG_HTTP_SERVER_MAX_URI_LEN + sizeof(WEB_FS_BASE_PATH) + 4];
    char uri_buf[CONFIG_HTTP_SERVER_MAX_URI_LEN + 1];
    const char *uri;

    // Validate and copy URI to bounded buffer
    size_t uri_len = strlen(req->uri);
    if (uri_len > CONFIG_HTTP_SERVER_MAX_URI_LEN) {
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
        snprintf(cookie, sizeof(cookie), "session=%s; Path=/; HttpOnly; SameSite=Strict%s", token, COOKIE_SECURE_FLAG);
        httpd_resp_set_hdr(req, "Set-Cookie", cookie);

        // Check if password change is required (device unclaimed)
        if (!system_state_is_claimed()) {
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
    httpd_resp_set_hdr(req, "Set-Cookie", COOKIE_CLEAR);
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
        httpd_resp_set_hdr(req, "Set-Cookie", COOKIE_CLEAR);
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
// Wipes ALL NVS data + encryption key. Device reboots as fresh-out-of-box.
static esp_err_t api_system_factory_reset(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");

    // Invalidate all in-memory sessions immediately
    web_auth_invalidate_all_sessions();

    // Set FIRST_BOOT LED state (visual feedback before reboot)
    event_bus_set_state(EVENT_FIRST_BOOT);

    // Erase entire NVS partition (all namespaces: cred, web_auth, WiFi, etc.)
    esp_err_t err = nvs_flash_erase();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase NVS partition: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "NVS partition erased");

    // Erase nvs_keys partition (fresh encryption key on next boot)
    const esp_partition_t *keys_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, "nvs_keys");
    if (keys_part != NULL) {
        err = esp_partition_erase_range(keys_part, 0, keys_part->size);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to erase nvs_keys: %s — will be regenerated on reboot",
                     esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "NVS encryption keys erased");
        }
    } else {
        ESP_LOGW(TAG, "nvs_keys partition not found — encryption key not erased");
    }

    ESP_LOGI(TAG, "Factory reset complete - scheduling reboot");

    // Clear session cookie
    httpd_resp_set_hdr(req, "Set-Cookie", COOKIE_CLEAR);
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

    const system_state_t *ss = system_state_get();
    bool ok = true;

    ok = ok && cJSON_AddStringToObject(json, "firmware_version", ss->firmware_version);
    ok = ok && cJSON_AddStringToObject(json, "device_name", ss->node_name);
    ok = ok && cJSON_AddNumberToObject(json, "uptime_seconds", ss->uptime_us / 1000000);
    ok = ok && cJSON_AddNumberToObject(json, "heap_free", ss->heap_free);
    ok = ok && cJSON_AddNumberToObject(json, "heap_total", ss->heap_total);
    ok = ok && cJSON_AddNumberToObject(json, "psram_free", ss->psram_free);
    ok = ok && cJSON_AddNumberToObject(json, "psram_total", ss->psram_total);

    ok = ok && cJSON_AddBoolToObject(json, "wifi_connected", ss->wifi.connected);
    if (ss->wifi.connected) {
        ok = ok && cJSON_AddStringToObject(json, "wifi_ssid", ss->wifi.ssid);
        ok = ok && cJSON_AddNumberToObject(json, "wifi_rssi", ss->wifi.rssi);
    }
    if (ss->wifi.ip[0] != '\0') {
        ok = ok && cJSON_AddStringToObject(json, "ip_address", ss->wifi.ip);
    }
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             ss->wifi.mac[0], ss->wifi.mac[1], ss->wifi.mac[2],
             ss->wifi.mac[3], ss->wifi.mac[4], ss->wifi.mac[5]);
    ok = ok && cJSON_AddStringToObject(json, "mac_address", mac_str);

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

    const system_state_t *ss = system_state_get();

    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON object");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    bool ok = true;
    ok = ok && cJSON_AddStringToObject(json, "version", ss->firmware_version);
    ok = ok && cJSON_AddStringToObject(json, "idf_version", ss->idf_version);
    char chip_rev[8];
    snprintf(chip_rev, sizeof(chip_rev), "%d.%d", ss->chip_revision_major, ss->chip_revision_minor);
    ok = ok && cJSON_AddStringToObject(json, "chip_revision", chip_rev);
    ok = ok && cJSON_AddNumberToObject(json, "cores", ss->chip_cores);

    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             ss->wifi.mac[0], ss->wifi.mac[1], ss->wifi.mac[2],
             ss->wifi.mac[3], ss->wifi.mac[4], ss->wifi.mac[5]);
    ok = ok && cJSON_AddStringToObject(json, "mac", mac_str);

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
    if (!secrets_is_first_boot()) {
        ESP_LOGW(TAG, "QR endpoint accessed after first boot - credentials hidden");
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Credentials only available during first boot\"}");
        return ESP_OK;
    }

    char qr_json[256];
    esp_err_t err = secrets_get_qr_json(qr_json, sizeof(qr_json));

    if (err == ESP_OK) {
        httpd_resp_sendstr(req, qr_json);
    } else {
        ESP_LOGE(TAG, "Failed to get QR JSON: %s", esp_err_to_name(err));
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    return ESP_OK;
}

// Helper: convert motd_priority_t to string
static const char* motd_priority_str(motd_priority_t priority)
{
    switch (priority) {
        case MOTD_PRIORITY_INFO:     return "info";
        case MOTD_PRIORITY_WARNING:  return "warning";
        case MOTD_PRIORITY_CRITICAL: return "critical";
        default:                     return "info";
    }
}

// GET /api/v1/system/motd
static esp_err_t api_system_motd_get(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK;
    }

    motd_entry_t motds[EVENT_BUS_MAX_MOTDS];
    size_t count = 0;
    event_bus_get_motds(motds, EVENT_BUS_MAX_MOTDS, &count);

    cJSON *arr = cJSON_CreateArray();
    if (arr == NULL) {
        ESP_LOGE(TAG, "Failed to create MOTD JSON array");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    bool ok = true;
    for (size_t i = 0; i < count && ok; i++) {
        cJSON *item = cJSON_CreateObject();
        if (item == NULL) {
            ok = false;
            break;
        }
        ok = ok && cJSON_AddNumberToObject(item, "id", motds[i].id);
        ok = ok && cJSON_AddStringToObject(item, "source", motds[i].source);
        ok = ok && cJSON_AddStringToObject(item, "message", motds[i].message);
        ok = ok && cJSON_AddStringToObject(item, "priority", motd_priority_str(motds[i].priority));
        ok = ok && cJSON_AddNumberToObject(item, "timestamp", motds[i].timestamp);
        if (ok) {
            cJSON_AddItemToArray(arr, item);
        } else {
            cJSON_Delete(item);
        }
    }

    if (!ok) {
        ESP_LOGE(TAG, "Failed to build MOTD JSON - memory allocation failed");
        cJSON_Delete(arr);
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char *resp = cJSON_PrintUnformatted(arr);
    cJSON_Delete(arr);

    if (!resp) {
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    free(resp);

    return ESP_OK;
}

// DELETE /api/v1/system/motd
static esp_err_t api_system_motd_delete(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK;
    }

    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret < 0) {
        ESP_LOGD(TAG, "MOTD delete recv error: %d", ret);
        return ESP_FAIL;
    }
    if (ret == 0) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"No body\"}");
        return ESP_OK;
    }
    buf[ret] = '\0';

    cJSON *json = cJSON_Parse(buf);
    if (!json) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Invalid JSON\"}");
        return ESP_OK;
    }

    cJSON *id_json = cJSON_GetObjectItem(json, "id");
    if (!cJSON_IsNumber(id_json)) {
        cJSON_Delete(json);
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Missing or invalid id\"}");
        return ESP_OK;
    }

    uint32_t id = (uint32_t)id_json->valueint;
    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");

    esp_err_t err = event_bus_dismiss_motd(id);
    if (err == ESP_OK) {
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else {
        httpd_resp_set_status(req, "404 Not Found");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"MOTD not found\"}");
    }

    return ESP_OK;
}

// GET /api/v1/ota - get OTA status
static esp_err_t api_ota_status_get(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK;
    }

    // Check for ?refresh=true query parameter
    char query[32] = {0};
    if (httpd_req_get_url_query_str(req, query, sizeof(query)) == ESP_OK) {
        char val[8] = {0};
        if (httpd_query_key_value(query, "refresh", val, sizeof(val)) == ESP_OK) {
            if (strcmp(val, "true") == 0) {
                esp_err_t check_err = ota_manager_check_now();
                if (check_err != ESP_OK) {
                    ESP_LOGW(TAG, "OTA refresh check failed: %s", esp_err_to_name(check_err));
                }
            }
        }
    }

    cJSON *json = cJSON_CreateObject();
    if (json == NULL) {
        ESP_LOGE(TAG, "Failed to create OTA status JSON");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    bool ok = true;
    ok = ok && cJSON_AddStringToObject(json, "state",
             system_ota_state_name(system_state_get()->ota.state));
    ok = ok && cJSON_AddStringToObject(json, "version", BOORKER_VERSION_STRING);

    ota_update_info_t update;
    if (ota_manager_get_available_update(&update) == ESP_OK) {
        cJSON *upd = cJSON_CreateObject();
        if (upd != NULL) {
            ok = ok && cJSON_AddStringToObject(upd, "version", update.version);
            ok = ok && cJSON_AddStringToObject(upd, "tag_name", update.tag_name);
            ok = ok && cJSON_AddStringToObject(upd, "release_notes", update.release_notes);
            ok = ok && cJSON_AddNumberToObject(upd, "size_bytes", update.size_bytes);
            ok = ok && cJSON_AddBoolToObject(upd, "is_prerelease", update.is_prerelease);
            ok = ok && cJSON_AddBoolToObject(upd, "has_sha256", update.has_sha256);
            ok = ok && cJSON_AddItemToObject(json, "update", upd);
        } else {
            ok = false;
        }
    }

    if (!ok) {
        ESP_LOGE(TAG, "Failed to build OTA status JSON - memory allocation failed");
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

// PUT /api/v1/ota - start GitHub update
static esp_err_t api_ota_update_put(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");

    // TODO: start_update is blocking (downloads entire firmware). The HTTP server
    // thread is unavailable during this time. Consider dispatching to a separate
    // task and returning 202 Accepted for async operation.
    esp_err_t err = ota_manager_start_update(NULL, NULL);
    if (err == ESP_OK) {
        httpd_resp_sendstr(req, "{\"success\":true}");
    } else if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"No update available or already in progress\"}");
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"OTA update failed\"}");
    }

    return ESP_OK;
}

// POST /api/v1/ota - upload firmware
static esp_err_t api_ota_upload_post(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");

    // Get content length
    int content_len = req->content_len;
    if (content_len <= 0) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"No content\"}");
        return ESP_OK;
    }

    // Optional X-SHA256 header
    char sha256_buf[65] = {0};
    const char *sha256 = NULL;
    if (httpd_req_get_hdr_value_str(req, "X-SHA256", sha256_buf, sizeof(sha256_buf)) == ESP_OK) {
        sha256 = sha256_buf;
    }

    // Start upload session
    esp_err_t err = ota_manager_start_upload((uint32_t)content_len, sha256, NULL, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start OTA upload: %s", esp_err_to_name(err));
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Failed to start upload\"}");
        return ESP_OK;
    }

    // Read body in chunks and write to OTA partition
    char buf[1024];
    int remaining = content_len;

    while (remaining > 0) {
        int to_read = remaining < (int)sizeof(buf) ? remaining : (int)sizeof(buf);
        int received = httpd_req_recv(req, buf, to_read);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) {
                continue;  // Retry on timeout
            }
            ESP_LOGE(TAG, "OTA upload recv error: %d", received);
            ota_manager_abort();
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Upload receive failed\"}");
            return ESP_OK;
        }

        err = ota_manager_write_upload_chunk(buf, received);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "OTA write chunk failed: %s", esp_err_to_name(err));
            ota_manager_abort();
            httpd_resp_set_status(req, "500 Internal Server Error");
            httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Write failed\"}");
            return ESP_OK;
        }

        remaining -= received;
    }

    // Finalize
    err = ota_manager_finish_upload();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA finish upload failed: %s", esp_err_to_name(err));
        ota_manager_abort();
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Verification failed\"}");
        return ESP_OK;
    }

    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

// DELETE /api/v1/ota - abort in-progress update
static esp_err_t api_ota_abort_delete(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK;
    }

    esp_err_t err = ota_manager_abort();
    if (err != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Abort failed\"}");
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
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

#if CONFIG_HTTP_SERVER_HTTPS_ENABLED
static esp_err_t redirect_to_https(httpd_req_t *req)
{
    char host[64] = {0};
    if (httpd_req_get_hdr_value_str(req, "Host", host, sizeof(host)) != ESP_OK) {
        const system_state_t *ss_redirect = system_state_get();
        if (ss_redirect->wifi.ip[0] != '\0') {
            strncpy(host, ss_redirect->wifi.ip, sizeof(host) - 1);
        }
        if (host[0] == '\0') {
            ESP_LOGW(TAG, "Cannot determine host for HTTPS redirect");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }
    }
    // Strip port from host if present (e.g. "192.168.1.1:80" → "192.168.1.1")
    char *colon = strchr(host, ':');
    if (colon) *colon = '\0';

    // Bound the URI to our configured max — snprintf truncates safely.
    // req->uri may report up to 512 bytes to the compiler but our redirect
    // only needs enough to get the browser to the right HTTPS page.
    char uri[CONFIG_HTTP_SERVER_MAX_URI_LEN + 1];
    strncpy(uri, req->uri, sizeof(uri) - 1);
    uri[sizeof(uri) - 1] = '\0';

    // "https://" (8) + host (64) + uri (MAX_URI_LEN) + null
    char location[8 + sizeof(host) + sizeof(uri)];
    snprintf(location, sizeof(location), "https://%s%s", host, uri);

    httpd_resp_set_status(req, "301 Moved Permanently");
    httpd_resp_set_hdr(req, "Location", location);
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}
#endif

esp_err_t http_server_start(void)
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

    // Configure and start server
#if CONFIG_HTTP_SERVER_HTTPS_ENABLED
    const char *cert = secrets_get_tls_cert();
    const char *pkey = secrets_get_tls_key();
    if (cert == NULL || pkey == NULL) {
        ESP_LOGE(TAG, "TLS cert/key not available — cannot start HTTPS");
        return ESP_FAIL;
    }

    httpd_ssl_config_t ssl_config = HTTPD_SSL_CONFIG_DEFAULT();
    ssl_config.servercert = (const uint8_t *)cert;
    ssl_config.servercert_len = strlen(cert) + 1;
    ssl_config.prvtkey_pem = (const uint8_t *)pkey;
    ssl_config.prvtkey_len = strlen(pkey) + 1;
    ssl_config.httpd.max_open_sockets = CONFIG_HTTP_SERVER_MAX_CONNECTIONS;
    ssl_config.httpd.max_uri_handlers = 20;
    ssl_config.httpd.stack_size = CONFIG_HTTP_SERVER_STACK_SIZE;
    ssl_config.httpd.uri_match_fn = httpd_uri_match_wildcard;

    ret = httpd_ssl_start(&s_server, &ssl_config);
#else
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = CONFIG_HTTP_SERVER_PORT;
    config.uri_match_fn = httpd_uri_match_wildcard;
    config.max_uri_handlers = 20;
    config.max_open_sockets = CONFIG_HTTP_SERVER_MAX_CONNECTIONS;
    config.stack_size = CONFIG_HTTP_SERVER_STACK_SIZE;

    ret = httpd_start(&s_server, &config);
#endif
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

    httpd_uri_t uri_motd_get = {
        .uri = "/api/v1/system/motd",
        .method = HTTP_GET,
        .handler = api_system_motd_get,
    };
    ret = httpd_register_uri_handler(s_server, &uri_motd_get);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register GET /api/v1/system/motd: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    httpd_uri_t uri_motd_delete = {
        .uri = "/api/v1/system/motd",
        .method = HTTP_DELETE,
        .handler = api_system_motd_delete,
    };
    ret = httpd_register_uri_handler(s_server, &uri_motd_delete);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register DELETE /api/v1/system/motd: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    // OTA endpoints
    httpd_uri_t uri_ota_get = {
        .uri = "/api/v1/ota",
        .method = HTTP_GET,
        .handler = api_ota_status_get,
    };
    ret = httpd_register_uri_handler(s_server, &uri_ota_get);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register GET /api/v1/ota: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    httpd_uri_t uri_ota_put = {
        .uri = "/api/v1/ota",
        .method = HTTP_PUT,
        .handler = api_ota_update_put,
    };
    ret = httpd_register_uri_handler(s_server, &uri_ota_put);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register PUT /api/v1/ota: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    httpd_uri_t uri_ota_post = {
        .uri = "/api/v1/ota",
        .method = HTTP_POST,
        .handler = api_ota_upload_post,
    };
    ret = httpd_register_uri_handler(s_server, &uri_ota_post);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register POST /api/v1/ota: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    httpd_uri_t uri_ota_delete = {
        .uri = "/api/v1/ota",
        .method = HTTP_DELETE,
        .handler = api_ota_abort_delete,
    };
    ret = httpd_register_uri_handler(s_server, &uri_ota_delete);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register DELETE /api/v1/ota: %s", esp_err_to_name(ret));
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

#if CONFIG_HTTP_SERVER_HTTPS_ENABLED
    // Start HTTP redirect server on port 80 (redirects all requests to HTTPS)
    httpd_config_t redirect_config = HTTPD_DEFAULT_CONFIG();
    redirect_config.server_port = 80;
    redirect_config.max_uri_handlers = 4;
    redirect_config.max_open_sockets = 2;
    redirect_config.stack_size = 4096;

    ret = httpd_start(&s_redirect_server, &redirect_config);
    if (ret == ESP_OK) {
        // Register redirect for all methods the HTTPS server handles
        const httpd_method_t methods[] = {HTTP_GET, HTTP_POST, HTTP_PUT, HTTP_DELETE};
        const char *method_names[] = {"GET", "POST", "PUT", "DELETE"};
        bool handler_ok = true;
        for (size_t i = 0; i < sizeof(methods) / sizeof(methods[0]); i++) {
            httpd_uri_t redirect_uri = {
                .uri = "/*",
                .method = methods[i],
                .handler = redirect_to_https,
            };
            if (httpd_register_uri_handler(s_redirect_server, &redirect_uri) != ESP_OK) {
                ESP_LOGW(TAG, "Failed to register redirect for %s", method_names[i]);
                handler_ok = false;
            }
        }
        if (handler_ok) {
            ESP_LOGI(TAG, "HTTP redirect server started on port 80");
        } else {
            ESP_LOGW(TAG, "HTTP redirect server started but some handlers failed");
        }
    } else {
        ESP_LOGW(TAG, "Failed to start HTTP redirect server: %s", esp_err_to_name(ret));
    }

    ESP_LOGI(TAG, "HTTPS server started on port 443");
#else
    ESP_LOGI(TAG, "Web server started on port %d", CONFIG_HTTP_SERVER_PORT);
#endif
    return ESP_OK;  // Server is running, even if some handlers failed
}

esp_err_t http_server_stop(void)
{
    if (!s_server) {
        return ESP_OK;
    }

#if CONFIG_HTTP_SERVER_HTTPS_ENABLED
    if (s_redirect_server) {
        httpd_stop(s_redirect_server);
        s_redirect_server = NULL;
    }
    esp_err_t ret = httpd_ssl_stop(s_server);
#else
    esp_err_t ret = httpd_stop(s_server);
#endif
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Server stop failed: %s", esp_err_to_name(ret));
    }
    s_server = NULL;

    esp_vfs_littlefs_unregister("storage");
    ESP_LOGI(TAG, "Web server stopped");

    return ret;
}

bool http_server_is_running(void)
{
    return s_server != NULL;
}

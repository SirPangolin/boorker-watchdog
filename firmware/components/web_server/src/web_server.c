#include "web_server.h"
#include "web_auth.h"
#include "device_identity.h"
#include "esp_log.h"
#include "esp_littlefs.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "esp_mac.h"
#include "cJSON.h"
#include <string.h>
#include <sys/stat.h>

static const char *TAG = "web_server";

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

// Check for path traversal attempts
static bool is_safe_path(const char *uri)
{
    // Reject path traversal attempts
    if (strstr(uri, "..") != NULL) {
        return false;
    }
    // Also check URL-encoded variants
    if (strstr(uri, "%2e%2e") != NULL || strstr(uri, "%2E%2E") != NULL) {
        return false;
    }
    return true;
}

// Serve static files from LittleFS with gzip support
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wformat-truncation"
static esp_err_t file_handler(httpd_req_t *req)
{
    // /littlefs + uri + .gz + null = max path
    char filepath[CONFIG_WEB_SERVER_MAX_URI_LEN + 16];
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
    snprintf(filepath, sizeof(filepath), "/littlefs%s.gz", uri);

    struct stat st;
    bool gzipped = false;

    if (stat(filepath, &st) == 0) {
        gzipped = true;
    } else {
        // Try non-gzipped
        snprintf(filepath, sizeof(filepath), "/littlefs%s", uri);
        if (stat(filepath, &st) != 0) {
            // SPA fallback: serve index.html for unknown paths
            snprintf(filepath, sizeof(filepath), "/littlefs/index.html.gz");
            if (stat(filepath, &st) == 0) {
                gzipped = true;
            } else {
                snprintf(filepath, sizeof(filepath), "/littlefs/index.html");
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
    while ((read_bytes = fread(buf, 1, sizeof(buf), f)) > 0) {
        if (httpd_resp_send_chunk(req, buf, read_bytes) != ESP_OK) {
            fclose(f);
            return ESP_FAIL;
        }
    }
    fclose(f);
    httpd_resp_send_chunk(req, NULL, 0);

    return ESP_OK;
}
#pragma GCC diagnostic pop

// POST /api/v1/auth/login
static esp_err_t api_auth_login(httpd_req_t *req)
{
    char buf[256];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret <= 0) {
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
        httpd_resp_sendstr(req, "{\"success\":true}");
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

    // Clear cookie
    httpd_resp_set_hdr(req, "Set-Cookie", "session=; Path=/; Max-Age=0; SameSite=Strict");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"success\":true}");
    return ESP_OK;
}

// GET /api/v1/system/status
static esp_err_t api_system_status(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK; // Auth failed, response already sent
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "uptime", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(json, "heap_free", esp_get_free_heap_size());

    const device_identity_t *id = device_identity_get();
    if (id) {
        cJSON_AddStringToObject(json, "node_name", id->node_name);
    }

    // Add power stub (to be implemented with actual power monitoring)
    cJSON *power = cJSON_CreateObject();
    cJSON_AddStringToObject(power, "source", "unknown");
    cJSON_AddBoolToObject(power, "ac_present", true);
    cJSON_AddItemToObject(json, "power", power);

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
    cJSON_AddNumberToObject(json, "chip_revision", chip_info.revision);
    cJSON_AddNumberToObject(json, "cores", chip_info.cores);

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    cJSON_AddStringToObject(json, "mac", mac_str);

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

// Mount LittleFS
static esp_err_t mount_littlefs(void)
{
    esp_vfs_littlefs_conf_t conf = {
        .base_path = "/littlefs",
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
    esp_littlefs_info("storage", &total, &used);
    ESP_LOGI(TAG, "LittleFS: %d/%d bytes used", (int)used, (int)total);

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

    // Register API endpoints
    httpd_uri_t uri_login = {
        .uri = "/api/v1/auth/login",
        .method = HTTP_POST,
        .handler = api_auth_login,
    };
    httpd_register_uri_handler(s_server, &uri_login);

    httpd_uri_t uri_logout = {
        .uri = "/api/v1/auth/logout",
        .method = HTTP_POST,
        .handler = api_auth_logout,
    };
    httpd_register_uri_handler(s_server, &uri_logout);

    httpd_uri_t uri_status = {
        .uri = "/api/v1/system/status",
        .method = HTTP_GET,
        .handler = api_system_status,
    };
    httpd_register_uri_handler(s_server, &uri_status);

    httpd_uri_t uri_info = {
        .uri = "/api/v1/system/info",
        .method = HTTP_GET,
        .handler = api_system_info,
    };
    httpd_register_uri_handler(s_server, &uri_info);

    // File handler (must be last - wildcard)
    httpd_uri_t uri_files = {
        .uri = "/*",
        .method = HTTP_GET,
        .handler = file_handler,
    };
    httpd_register_uri_handler(s_server, &uri_files);

    ESP_LOGI(TAG, "Web server started on port %d", CONFIG_WEB_SERVER_PORT);
    return ESP_OK;
}

esp_err_t web_server_stop(void)
{
    if (!s_server) {
        return ESP_OK;
    }

    httpd_stop(s_server);
    s_server = NULL;

    esp_vfs_littlefs_unregister("storage");
    ESP_LOGI(TAG, "Web server stopped");

    return ESP_OK;
}

bool web_server_is_running(void)
{
    return s_server != NULL;
}

#include "web_auth.h"
#include "device_identity.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mbedtls/sha256.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "web_auth";

#define NVS_NAMESPACE "web_auth"
#define NVS_KEY_PASS_HASH "pass_hash"
#define NVS_KEY_PASS_SALT "pass_salt"
#define NVS_KEY_PASS_CHANGED "pass_chgd"

#define SALT_LEN 16
#define HASH_LEN 32
#define MAX_FAILED_ATTEMPTS 5
#define LOCKOUT_SECONDS 300

typedef struct {
    char token[WEB_AUTH_SESSION_TOKEN_LEN + 1];
    time_t created;
    bool valid;
} session_t;

static session_t s_sessions[WEB_AUTH_MAX_SESSIONS];
static uint8_t s_password_hash[HASH_LEN];
static uint8_t s_password_salt[SALT_LEN];
static bool s_password_changed = false;
static bool s_initialized = false;
static int s_failed_attempts = 0;
static time_t s_lockout_until = 0;

static void hash_password(const char *password, const uint8_t *salt, uint8_t *hash_out)
{
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);
    mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256
    mbedtls_sha256_update(&ctx, salt, SALT_LEN);
    mbedtls_sha256_update(&ctx, (const uint8_t *)password, strlen(password));
    mbedtls_sha256_finish(&ctx, hash_out);
    mbedtls_sha256_free(&ctx);
}

static void generate_token(char *token_out)
{
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    for (int i = 0; i < WEB_AUTH_SESSION_TOKEN_LEN; i++) {
        token_out[i] = chars[esp_random() % (sizeof(chars) - 1)];
    }
    token_out[WEB_AUTH_SESSION_TOKEN_LEN] = '\0';
}

static esp_err_t load_or_create_password(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t len = HASH_LEN;
    ret = nvs_get_blob(handle, NVS_KEY_PASS_HASH, s_password_hash, &len);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // First time - hash the default password from device_identity
        const device_identity_t *id = device_identity_get();
        if (id == NULL) {
            ESP_LOGE(TAG, "device_identity not initialized");
            nvs_close(handle);
            return ESP_ERR_INVALID_STATE;
        }

        // Generate random salt
        esp_fill_random(s_password_salt, SALT_LEN);

        // Hash default password
        hash_password(id->web_password, s_password_salt, s_password_hash);

        // Store in NVS
        ret = nvs_set_blob(handle, NVS_KEY_PASS_HASH, s_password_hash, HASH_LEN);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set pass_hash: %s", esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }

        ret = nvs_set_blob(handle, NVS_KEY_PASS_SALT, s_password_salt, SALT_LEN);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set pass_salt: %s", esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }

        ret = nvs_set_u8(handle, NVS_KEY_PASS_CHANGED, 0);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set pass_changed: %s", esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }

        ret = nvs_commit(handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }

        ESP_LOGI(TAG, "Default password hash stored");
    } else if (ret == ESP_OK) {
        // Load salt and changed flag
        len = SALT_LEN;
        ret = nvs_get_blob(handle, NVS_KEY_PASS_SALT, s_password_salt, &len);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to read pass_salt: %s", esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }

        uint8_t changed = 0;
        nvs_get_u8(handle, NVS_KEY_PASS_CHANGED, &changed);
        s_password_changed = (changed == 1);
    } else {
        ESP_LOGE(TAG, "Failed to read pass_hash: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    nvs_close(handle);
    return ESP_OK;
}

esp_err_t web_auth_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    // Clear sessions
    memset(s_sessions, 0, sizeof(s_sessions));

    // Load or create password
    esp_err_t ret = load_or_create_password();
    if (ret != ESP_OK) {
        return ret;
    }

    s_initialized = true;
    ESP_LOGI(TAG, "Web auth initialized (password_changed=%d)", s_password_changed);
    return ESP_OK;
}

bool web_auth_password_changed(void)
{
    return s_password_changed;
}

esp_err_t web_auth_login(const char *username, const char *password, char *token_out)
{
    if (!s_initialized || username == NULL || password == NULL || token_out == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Check lockout
    time_t now = time(NULL);
    if (s_lockout_until > now) {
        ESP_LOGW(TAG, "Login attempt during lockout");
        return ESP_ERR_INVALID_STATE;
    }

    // Only admin user supported
    if (strcmp(username, "admin") != 0) {
        s_failed_attempts++;
        return ESP_ERR_INVALID_ARG;
    }

    // Hash provided password and compare
    uint8_t hash[HASH_LEN];
    hash_password(password, s_password_salt, hash);

    if (memcmp(hash, s_password_hash, HASH_LEN) != 0) {
        s_failed_attempts++;
        ESP_LOGW(TAG, "Failed login attempt (%d/%d)", s_failed_attempts, MAX_FAILED_ATTEMPTS);

        if (s_failed_attempts >= MAX_FAILED_ATTEMPTS) {
            s_lockout_until = now + LOCKOUT_SECONDS;
            ESP_LOGW(TAG, "Account locked for %d seconds", LOCKOUT_SECONDS);
        }
        return ESP_ERR_INVALID_ARG;
    }

    // Success - reset failed attempts
    s_failed_attempts = 0;

    // Find free session slot
    int slot = -1;
    for (int i = 0; i < WEB_AUTH_MAX_SESSIONS; i++) {
        if (!s_sessions[i].valid) {
            slot = i;
            break;
        }
    }

    // If no free slot, evict oldest
    if (slot < 0) {
        slot = 0;
        for (int i = 1; i < WEB_AUTH_MAX_SESSIONS; i++) {
            if (s_sessions[i].created < s_sessions[slot].created) {
                slot = i;
            }
        }
    }

    // Create session
    generate_token(s_sessions[slot].token);
    s_sessions[slot].created = now;
    s_sessions[slot].valid = true;

    strcpy(token_out, s_sessions[slot].token);
    ESP_LOGI(TAG, "Login successful, session created");
    return ESP_OK;
}

bool web_auth_validate_session(const char *token)
{
    if (!s_initialized || token == NULL) {
        return false;
    }

    for (int i = 0; i < WEB_AUTH_MAX_SESSIONS; i++) {
        if (s_sessions[i].valid && strcmp(s_sessions[i].token, token) == 0) {
            // Check expiry (1 hour)
            if (time(NULL) - s_sessions[i].created > 3600) {
                s_sessions[i].valid = false;
                return false;
            }
            return true;
        }
    }
    return false;
}

void web_auth_logout(const char *token)
{
    if (token == NULL) return;

    for (int i = 0; i < WEB_AUTH_MAX_SESSIONS; i++) {
        if (s_sessions[i].valid && strcmp(s_sessions[i].token, token) == 0) {
            s_sessions[i].valid = false;
            ESP_LOGI(TAG, "Session invalidated");
            return;
        }
    }
}

esp_err_t web_auth_change_password(const char *current_password, const char *new_password)
{
    if (!s_initialized || current_password == NULL || new_password == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Verify current password
    uint8_t hash[HASH_LEN];
    hash_password(current_password, s_password_salt, hash);

    if (memcmp(hash, s_password_hash, HASH_LEN) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    // Generate new salt and hash
    esp_fill_random(s_password_salt, SALT_LEN);
    hash_password(new_password, s_password_salt, s_password_hash);

    // Store
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_blob(handle, NVS_KEY_PASS_HASH, s_password_hash, HASH_LEN);
    if (ret != ESP_OK) {
        nvs_close(handle);
        return ret;
    }

    ret = nvs_set_blob(handle, NVS_KEY_PASS_SALT, s_password_salt, SALT_LEN);
    if (ret != ESP_OK) {
        nvs_close(handle);
        return ret;
    }

    ret = nvs_set_u8(handle, NVS_KEY_PASS_CHANGED, 1);
    if (ret != ESP_OK) {
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    nvs_close(handle);

    s_password_changed = true;

    // Invalidate all sessions (force re-login)
    memset(s_sessions, 0, sizeof(s_sessions));

    ESP_LOGI(TAG, "Password changed");
    return ret;
}

bool web_auth_check_request(httpd_req_t *req)
{
    if (req == NULL) return false;

    char buf[128];

    // Check session cookie
    if (httpd_req_get_hdr_value_str(req, "Cookie", buf, sizeof(buf)) == ESP_OK) {
        char *session = strstr(buf, "session=");
        if (session) {
            session += 8; // Skip "session="
            char *end = strchr(session, ';');
            if (end) *end = '\0';

            if (web_auth_validate_session(session)) {
                return true;
            }
        }
    }

    return false;
}

esp_err_t web_auth_require(httpd_req_t *req)
{
    if (web_auth_check_request(req)) {
        return ESP_OK;
    }

    httpd_resp_set_status(req, "401 Unauthorized");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, "{\"error\":true,\"code\":\"UNAUTHORIZED\"}", HTTPD_RESP_USE_STRLEN);
    return ESP_FAIL;
}

int web_auth_get_attempts_remaining(void)
{
    time_t now = time(NULL);
    if (s_lockout_until > now) {
        return 0;
    }
    return MAX_FAILED_ATTEMPTS - s_failed_attempts;
}

esp_err_t web_auth_reset_password(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_erase_all(handle);
    if (ret != ESP_OK) {
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    nvs_close(handle);
    if (ret != ESP_OK) {
        return ret;
    }

    s_initialized = false;
    s_password_changed = false;

    return web_auth_init();
}

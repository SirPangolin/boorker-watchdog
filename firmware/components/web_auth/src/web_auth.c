#include "web_auth.h"
#include "device_identity.h"
#include "esp_log.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "mbedtls/sha256.h"
#include "mbedtls/constant_time.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

static const char *TAG = "web_auth";

#define NVS_NAMESPACE "web_auth"
#define NVS_KEY_PASS_HASH "pass_hash"
#define NVS_KEY_PASS_SALT "pass_salt"
#define NVS_KEY_PASS_CHANGED "pass_chgd"
#define NVS_KEY_LOCKOUT_UNTIL "lockout"
#define NVS_KEY_FAILED_ATTEMPTS "fail_cnt"

#define SALT_LEN 16
#define HASH_LEN 32

// Use Kconfig values with sensible defaults
#ifndef CONFIG_WEB_AUTH_DEFAULT_USERNAME
#define CONFIG_WEB_AUTH_DEFAULT_USERNAME "admin"
#endif
#ifndef CONFIG_WEB_AUTH_MAX_FAILED_ATTEMPTS
#define CONFIG_WEB_AUTH_MAX_FAILED_ATTEMPTS 5
#endif
#ifndef CONFIG_WEB_AUTH_LOCKOUT_SECONDS
#define CONFIG_WEB_AUTH_LOCKOUT_SECONDS 300
#endif
#ifndef CONFIG_WEB_AUTH_SESSION_EXPIRY_SECONDS
#define CONFIG_WEB_AUTH_SESSION_EXPIRY_SECONDS 3600
#endif

typedef struct {
    char token[WEB_AUTH_SESSION_TOKEN_LEN + 1];
    time_t created;
    bool valid;
} session_t;

// Mutex protects s_sessions array - HTTP handlers run concurrently
static SemaphoreHandle_t s_session_mutex = NULL;

static session_t s_sessions[WEB_AUTH_MAX_SESSIONS];
static uint8_t s_password_hash[HASH_LEN];
static uint8_t s_password_salt[SALT_LEN];
static bool s_password_changed = false;
static bool s_initialized = false;
static int s_failed_attempts = 0;
static time_t s_lockout_until = 0;

static esp_err_t hash_password(const char *password, const uint8_t *salt, uint8_t *hash_out)
{
    mbedtls_sha256_context ctx;
    mbedtls_sha256_init(&ctx);

    int ret = mbedtls_sha256_starts(&ctx, 0); // 0 = SHA-256
    if (ret != 0) {
        ESP_LOGE(TAG, "SHA-256 start failed: %d", ret);
        mbedtls_sha256_free(&ctx);
        return ESP_FAIL;
    }

    ret = mbedtls_sha256_update(&ctx, salt, SALT_LEN);
    if (ret != 0) {
        ESP_LOGE(TAG, "SHA-256 update (salt) failed: %d", ret);
        mbedtls_sha256_free(&ctx);
        return ESP_FAIL;
    }

    ret = mbedtls_sha256_update(&ctx, (const uint8_t *)password, strlen(password));
    if (ret != 0) {
        ESP_LOGE(TAG, "SHA-256 update (password) failed: %d", ret);
        mbedtls_sha256_free(&ctx);
        return ESP_FAIL;
    }

    ret = mbedtls_sha256_finish(&ctx, hash_out);
    if (ret != 0) {
        ESP_LOGE(TAG, "SHA-256 finish failed: %d", ret);
        mbedtls_sha256_free(&ctx);
        return ESP_FAIL;
    }

    mbedtls_sha256_free(&ctx);
    return ESP_OK;
}

// Check if token already exists in sessions (collision check)
// Note: Must be called with s_session_mutex held
static bool token_exists_in_sessions(const char *token)
{
    for (int i = 0; i < WEB_AUTH_MAX_SESSIONS; i++) {
        if (s_sessions[i].valid &&
            memcmp(s_sessions[i].token, token, WEB_AUTH_SESSION_TOKEN_LEN) == 0) {
            return true;
        }
    }
    return false;
}

// Generate unique session token (checks for collisions)
// Note: Must be called with s_session_mutex held
static void generate_token(char *token_out)
{
    static const char chars[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    const uint32_t charset_len = sizeof(chars) - 1; // 62 characters
    const int max_attempts = 3;  // Collision extremely unlikely with 32-char token

    for (int attempt = 0; attempt < max_attempts; attempt++) {
        for (int i = 0; i < WEB_AUTH_SESSION_TOKEN_LEN; i++) {
            // Use multiply-and-shift to avoid modulo bias
            uint32_t index = (uint32_t)(((uint64_t)esp_random() * charset_len) >> 32);
            token_out[i] = chars[index];
        }
        token_out[WEB_AUTH_SESSION_TOKEN_LEN] = '\0';

        // Check for uniqueness (extremely rare collision, but defense in depth)
        if (!token_exists_in_sessions(token_out)) {
            return;
        }
        ESP_LOGW(TAG, "Token collision detected (attempt %d/%d), regenerating",
                 attempt + 1, max_attempts);
    }
    // If we get here, something is very wrong (3 collisions in a row)
    ESP_LOGE(TAG, "Failed to generate unique token after %d attempts", max_attempts);
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
        esp_err_t hash_ret = hash_password(id->web_password, s_password_salt, s_password_hash);
        if (hash_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to hash default password");
            nvs_close(handle);
            return hash_ret;
        }

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
        ret = nvs_get_u8(handle, NVS_KEY_PASS_CHANGED, &changed);
        if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to read pass_changed: %s", esp_err_to_name(ret));
            // Continue with default (not changed) - non-fatal
        }
        s_password_changed = (changed == 1);
    } else {
        ESP_LOGE(TAG, "Failed to read pass_hash: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    nvs_close(handle);
    return ESP_OK;
}

static esp_err_t load_lockout_state(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        // NVS not available or namespace doesn't exist - use defaults
        return ESP_OK;
    }

    uint32_t lockout_time = 0;
    ret = nvs_get_u32(handle, NVS_KEY_LOCKOUT_UNTIL, &lockout_time);
    if (ret == ESP_OK) {
        s_lockout_until = (time_t)lockout_time;
    }

    uint8_t attempts = 0;
    ret = nvs_get_u8(handle, NVS_KEY_FAILED_ATTEMPTS, &attempts);
    if (ret == ESP_OK) {
        s_failed_attempts = (int)attempts;
    }

    nvs_close(handle);
    return ESP_OK;
}

static esp_err_t save_lockout_state(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS for lockout state: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_u32(handle, NVS_KEY_LOCKOUT_UNTIL, (uint32_t)s_lockout_until);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save lockout_until: %s", esp_err_to_name(ret));
    }

    ret = nvs_set_u8(handle, NVS_KEY_FAILED_ATTEMPTS, (uint8_t)s_failed_attempts);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save failed_attempts: %s", esp_err_to_name(ret));
    }

    nvs_commit(handle);
    nvs_close(handle);
    return ESP_OK;
}

esp_err_t web_auth_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    // Create session mutex
    if (s_session_mutex == NULL) {
        s_session_mutex = xSemaphoreCreateMutex();
        if (s_session_mutex == NULL) {
            ESP_LOGE(TAG, "Failed to create session mutex");
            return ESP_ERR_NO_MEM;
        }
    }

    // Clear sessions
    memset(s_sessions, 0, sizeof(s_sessions));

    // Load or create password
    esp_err_t ret = load_or_create_password();
    if (ret != ESP_OK) {
        return ret;
    }

    // Load persisted lockout state (survives reboot)
    load_lockout_state();

    s_initialized = true;
    ESP_LOGI(TAG, "Web auth initialized (password_changed=%d, failed_attempts=%d)",
             s_password_changed, s_failed_attempts);
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

    // Only configured admin user supported
    if (strcmp(username, CONFIG_WEB_AUTH_DEFAULT_USERNAME) != 0) {
        s_failed_attempts++;
        return ESP_ERR_INVALID_ARG;
    }

    // Hash provided password and compare
    uint8_t hash[HASH_LEN];
    if (hash_password(password, s_password_salt, hash) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to hash login password");
        return ESP_ERR_NO_MEM;
    }

    if (mbedtls_ct_memcmp(hash, s_password_hash, HASH_LEN) != 0) {
        s_failed_attempts++;
        ESP_LOGW(TAG, "Failed login attempt (%d/%d)", s_failed_attempts, CONFIG_WEB_AUTH_MAX_FAILED_ATTEMPTS);

        if (s_failed_attempts >= CONFIG_WEB_AUTH_MAX_FAILED_ATTEMPTS) {
            s_lockout_until = now + CONFIG_WEB_AUTH_LOCKOUT_SECONDS;
            ESP_LOGW(TAG, "Account locked for %d seconds", CONFIG_WEB_AUTH_LOCKOUT_SECONDS);
        }
        // Persist lockout state so it survives reboot
        save_lockout_state();
        return ESP_ERR_INVALID_ARG;
    }

    // Success - reset failed attempts and persist
    s_failed_attempts = 0;
    s_lockout_until = 0;
    save_lockout_state();

    // Lock mutex before accessing s_sessions
    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to acquire session mutex");
        return ESP_ERR_TIMEOUT;
    }

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

    strncpy(token_out, s_sessions[slot].token, WEB_AUTH_SESSION_TOKEN_LEN);
    token_out[WEB_AUTH_SESSION_TOKEN_LEN] = '\0';

    xSemaphoreGive(s_session_mutex);

    ESP_LOGI(TAG, "Login successful, session created");
    return ESP_OK;
}

bool web_auth_validate_session(const char *token)
{
    if (!s_initialized || token == NULL) {
        ESP_LOGD(TAG, "Session validation failed: not initialized or null token");
        return false;
    }

    if (s_session_mutex == NULL) {
        ESP_LOGE(TAG, "Session mutex not initialized");
        return false;
    }

    if (xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire session mutex for validation");
        return false;
    }

    bool result = false;
    size_t token_len = strlen(token);

    for (int i = 0; i < WEB_AUTH_MAX_SESSIONS; i++) {
        if (s_sessions[i].valid) {
            // Constant-time comparison prevents timing attacks
            if (token_len == WEB_AUTH_SESSION_TOKEN_LEN &&
                mbedtls_ct_memcmp(s_sessions[i].token, token, WEB_AUTH_SESSION_TOKEN_LEN) == 0) {
                // Check expiry (configurable, default 1 hour)
                if (time(NULL) - s_sessions[i].created > CONFIG_WEB_AUTH_SESSION_EXPIRY_SECONDS) {
                    s_sessions[i].valid = false;
                    ESP_LOGD(TAG, "Session expired");
                } else {
                    result = true;
                }
                break;
            }
        }
    }

    xSemaphoreGive(s_session_mutex);

    if (!result && token_len > 0) {
        ESP_LOGD(TAG, "Session validation failed: token not found or expired");
    }
    return result;
}

bool web_auth_logout(const char *token)
{
    if (token == NULL) {
        return false;
    }

    if (s_session_mutex == NULL ||
        xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Failed to acquire session mutex for logout");
        return false;
    }

    bool found = false;
    size_t token_len = strlen(token);
    for (int i = 0; i < WEB_AUTH_MAX_SESSIONS; i++) {
        if (s_sessions[i].valid) {
            // Constant-time comparison prevents timing attacks
            if (token_len == WEB_AUTH_SESSION_TOKEN_LEN &&
                mbedtls_ct_memcmp(s_sessions[i].token, token, WEB_AUTH_SESSION_TOKEN_LEN) == 0) {
                s_sessions[i].valid = false;
                found = true;
                ESP_LOGI(TAG, "Session invalidated");
                break;
            }
        }
    }

    xSemaphoreGive(s_session_mutex);

    if (!found) {
        ESP_LOGW(TAG, "Logout requested but session not found");
    }
    return found;
}

esp_err_t web_auth_change_password(const char *current_password, const char *new_password)
{
    if (!s_initialized || current_password == NULL || new_password == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    // Defense-in-depth: validate password length in core function too
    size_t new_len = strlen(new_password);
    if (new_len < 8 || new_len > 64) {
        ESP_LOGW(TAG, "Password change rejected: length %zu not in range 8-64", new_len);
        return ESP_ERR_INVALID_SIZE;
    }

    // Verify current password
    uint8_t hash[HASH_LEN];
    if (hash_password(current_password, s_password_salt, hash) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to hash current password");
        return ESP_ERR_NO_MEM;
    }

    if (mbedtls_ct_memcmp(hash, s_password_hash, HASH_LEN) != 0) {
        ESP_LOGW(TAG, "Password change rejected: current password incorrect");
        return ESP_ERR_INVALID_ARG;
    }

    // Generate new salt and hash into temporary buffers
    // (don't modify global state until NVS write succeeds)
    uint8_t new_salt[SALT_LEN];
    uint8_t new_hash[HASH_LEN];
    esp_fill_random(new_salt, SALT_LEN);
    if (hash_password(new_password, new_salt, new_hash) != ESP_OK) {
        ESP_LOGE(TAG, "Failed to hash new password");
        return ESP_ERR_NO_MEM;
    }

    // Store to NVS first
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for password change: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_blob(handle, NVS_KEY_PASS_HASH, new_hash, HASH_LEN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write new password hash: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_set_blob(handle, NVS_KEY_PASS_SALT, new_salt, SALT_LEN);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write new password salt: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_set_u8(handle, NVS_KEY_PASS_CHANGED, 1);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write password changed flag: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    nvs_close(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit password change: %s", esp_err_to_name(ret));
        return ret;
    }

    // NVS succeeded - now update in-memory state
    memcpy(s_password_hash, new_hash, HASH_LEN);
    memcpy(s_password_salt, new_salt, SALT_LEN);
    s_password_changed = true;

    // Invalidate all sessions (force re-login) with mutex
    if (s_session_mutex != NULL &&
        xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(s_sessions, 0, sizeof(s_sessions));
        xSemaphoreGive(s_session_mutex);
    } else {
        ESP_LOGW(TAG, "Could not acquire mutex for session invalidation");
        memset(s_sessions, 0, sizeof(s_sessions));  // Still clear, just not atomic
    }

    ESP_LOGI(TAG, "Password changed");
    return ESP_OK;
}

void web_auth_invalidate_all_sessions(void)
{
    if (s_session_mutex != NULL &&
        xSemaphoreTake(s_session_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        memset(s_sessions, 0, sizeof(s_sessions));
        xSemaphoreGive(s_session_mutex);
    } else {
        ESP_LOGW(TAG, "Could not acquire mutex for session invalidation");
        memset(s_sessions, 0, sizeof(s_sessions));  // Still clear
    }
    ESP_LOGI(TAG, "All sessions invalidated");
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

            // Calculate token length safely
            size_t token_len = end ? (size_t)(end - session) : strlen(session);

            // Validate token length before use
            if (token_len > 0 && token_len <= WEB_AUTH_SESSION_TOKEN_LEN) {
                char token[WEB_AUTH_SESSION_TOKEN_LEN + 1];
                strncpy(token, session, token_len);
                token[token_len] = '\0';

                if (web_auth_validate_session(token)) {
                    return true;
                }
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
    return CONFIG_WEB_AUTH_MAX_FAILED_ATTEMPTS - s_failed_attempts;
}

esp_err_t web_auth_reset_password(void)
{
    ESP_LOGI(TAG, "Resetting password to default");

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for reset: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_erase_all(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    nvs_close(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS erase: %s", esp_err_to_name(ret));
        return ret;
    }

    s_initialized = false;
    s_password_changed = false;

    ESP_LOGI(TAG, "NVS cleared, re-initializing with default password");
    return web_auth_init();
}

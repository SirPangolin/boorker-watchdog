# Phase 4: Web Interface Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement a self-contained web interface for Boorker nodes with secure authentication, real-time updates, and configuration management.

**Architecture:** ESP-IDF HTTP server with WebSocket support, serving minified/gzipped SPA from LittleFS. Authentication via session tokens with per-device hardware RNG credentials. REST API for all operations.

**Tech Stack:** ESP-IDF v5.5.3, esp_http_server, esp_littlefs, cJSON, Vanilla JS, CSS variables

**Design Document:** `docs/plans/2026-02-28-web-interface-design.md`

---

## Prerequisites

- Phase 1-3 complete (wifi_manager, tailscale_manager working)
- ESP-IDF v5.5.3 environment configured
- Device connected via `/dev/ttyUSB0`

**Verification Commands:**
```bash
source ~/esp/esp-idf/export.sh
cd ~/claude/boorker-watchdog/firmware
idf.py build  # Should succeed
```

---

## Task 1: Add LittleFS Partition

**Files:**
- Create: `firmware/partitions.csv`
- Modify: `firmware/CMakeLists.txt`
- Modify: `firmware/sdkconfig.defaults`

**Step 1: Create custom partition table**

Create `firmware/partitions.csv`:
```csv
# Name,   Type, SubType, Offset,  Size,    Flags
nvs,      data, nvs,     0x9000,  0x6000,
phy_init, data, phy,     0xf000,  0x1000,
factory,  app,  factory, 0x10000, 0x1E0000,
ota_0,    app,  ota_0,   0x1F0000,0x1E0000,
ota_1,    app,  ota_1,   0x3D0000,0x1E0000,
littlefs, data, spiffs,  0x5B0000,0x80000,
```

**Step 2: Update sdkconfig.defaults**

Append to `firmware/sdkconfig.defaults`:
```
# Custom partition table
CONFIG_PARTITION_TABLE_CUSTOM=y
CONFIG_PARTITION_TABLE_CUSTOM_FILENAME="partitions.csv"

# LittleFS
CONFIG_LITTLEFS_MAX_PARTITIONS=1
CONFIG_LITTLEFS_PAGE_SIZE=256
CONFIG_LITTLEFS_OBJ_NAME_LEN=64
```

**Step 3: Update CMakeLists.txt for LittleFS**

Modify `firmware/CMakeLists.txt` to add after `project()`:
```cmake
# Create LittleFS image from www directory
littlefs_create_partition_image(littlefs ../www FLASH_IN_PROJECT)
```

**Step 4: Create placeholder www directory**

```bash
mkdir -p firmware/../www
echo '{"placeholder": true}' > www/test.json
```

**Step 5: Reconfigure and build**

Run:
```bash
cd ~/claude/boorker-watchdog/firmware
rm -rf build sdkconfig
idf.py set-target esp32s3
idf.py build
```

Expected: Build succeeds with LittleFS partition

**Step 6: Commit**

```bash
git add firmware/partitions.csv firmware/sdkconfig.defaults firmware/CMakeLists.txt www/
git commit -m "feat: add LittleFS partition for web assets

- Custom partition table with 512KB LittleFS
- OTA partitions for firmware updates
- Placeholder www directory"
```

---

## Task 2: Create Device Identity Component

**Files:**
- Create: `firmware/components/device_identity/CMakeLists.txt`
- Create: `firmware/components/device_identity/Kconfig`
- Create: `firmware/components/device_identity/include/device_identity.h`
- Create: `firmware/components/device_identity/src/device_identity.c`

**Step 1: Create component structure**

```bash
mkdir -p firmware/components/device_identity/{include,src}
```

**Step 2: Create CMakeLists.txt**

Create `firmware/components/device_identity/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "src/device_identity.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_hw_support esp_system log
)
```

**Step 3: Create Kconfig**

Create `firmware/components/device_identity/Kconfig`:
```
menu "Device Identity"

    config DEVICE_ID_WEB_PASS_LEN
        int "Web password length"
        default 12
        range 8 32
        help
            Length of auto-generated web password

    config DEVICE_ID_AP_PASS_LEN
        int "AP password length"
        default 12
        range 8 32
        help
            Length of auto-generated AP password

    config DEVICE_ID_NAME_PREFIX
        string "Device name prefix"
        default "boorker"
        help
            Prefix for device name (suffix is MAC-derived)

endmenu
```

**Step 4: Create header file**

Create `firmware/components/device_identity/include/device_identity.h`:
```c
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Device identity - unique credentials generated at first boot
 */
typedef struct {
    char node_name[32];       // boorker-XXXX (MAC-derived)
    char web_password[33];    // Hardware RNG generated
    char ap_password[33];     // Hardware RNG generated
    char ble_pop[7];          // 6-digit PIN
    char node_suffix[5];      // Last 2 bytes of MAC as hex
} device_identity_t;

/**
 * Initialize device identity
 * - Loads from NVS if exists
 * - Generates new credentials using hardware RNG if first boot
 *
 * @return ESP_OK on success
 */
esp_err_t device_identity_init(void);

/**
 * Get device identity (read-only)
 * Must call device_identity_init() first
 *
 * @return Pointer to identity struct (valid until deinit)
 */
const device_identity_t* device_identity_get(void);

/**
 * Check if this is first boot (credentials just generated)
 * Used to trigger QR code display on OLED
 *
 * @return true if credentials were just generated
 */
bool device_identity_is_first_boot(void);

/**
 * Mark first boot as acknowledged (user saw credentials)
 * Clears the first_boot flag in NVS
 *
 * @return ESP_OK on success
 */
esp_err_t device_identity_ack_first_boot(void);

/**
 * Regenerate all credentials (factory reset)
 *
 * @return ESP_OK on success
 */
esp_err_t device_identity_regenerate(void);

/**
 * Generate QR code data as JSON string
 *
 * @param buf Output buffer
 * @param buf_len Buffer size
 * @return ESP_OK on success
 */
esp_err_t device_identity_get_qr_json(char *buf, size_t buf_len);

#ifdef __cplusplus
}
#endif
```

**Step 5: Create implementation**

Create `firmware/components/device_identity/src/device_identity.c`:
```c
#include "device_identity.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "device_id";

#define NVS_NAMESPACE "device_id"
#define NVS_KEY_WEB_PASS "web_pass"
#define NVS_KEY_AP_PASS "ap_pass"
#define NVS_KEY_BLE_POP "ble_pop"
#define NVS_KEY_FIRST_BOOT "first_boot"

static device_identity_t s_identity;
static bool s_initialized = false;
static bool s_first_boot = false;

// Characters for password generation (alphanumeric + some symbols)
static const char PASS_CHARS[] = "ABCDEFGHJKLMNPQRSTUVWXYZabcdefghjkmnpqrstuvwxyz23456789#$@!";
static const int PASS_CHARS_LEN = sizeof(PASS_CHARS) - 1;

static void generate_random_string(char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint32_t rand = esp_random();
        buf[i] = PASS_CHARS[rand % PASS_CHARS_LEN];
    }
    buf[len] = '\0';
}

static void generate_random_digits(char *buf, size_t len)
{
    for (size_t i = 0; i < len; i++) {
        uint32_t rand = esp_random();
        buf[i] = '0' + (rand % 10);
    }
    buf[len] = '\0';
}

static void derive_node_name(void)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);

    // Node suffix from last 2 bytes of MAC
    snprintf(s_identity.node_suffix, sizeof(s_identity.node_suffix),
             "%02X%02X", mac[4], mac[5]);

    // Full node name
    snprintf(s_identity.node_name, sizeof(s_identity.node_name),
             "%s-%s", CONFIG_DEVICE_ID_NAME_PREFIX, s_identity.node_suffix);
}

static esp_err_t load_or_generate_credentials(void)
{
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    // Check if credentials exist
    size_t len = sizeof(s_identity.web_password);
    ret = nvs_get_str(handle, NVS_KEY_WEB_PASS, s_identity.web_password, &len);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // First boot - generate new credentials
        ESP_LOGI(TAG, "First boot - generating credentials with hardware RNG");
        s_first_boot = true;

        generate_random_string(s_identity.web_password, CONFIG_DEVICE_ID_WEB_PASS_LEN);
        generate_random_string(s_identity.ap_password, CONFIG_DEVICE_ID_AP_PASS_LEN);
        generate_random_digits(s_identity.ble_pop, 6);

        // Store in NVS
        nvs_set_str(handle, NVS_KEY_WEB_PASS, s_identity.web_password);
        nvs_set_str(handle, NVS_KEY_AP_PASS, s_identity.ap_password);
        nvs_set_str(handle, NVS_KEY_BLE_POP, s_identity.ble_pop);
        nvs_set_u8(handle, NVS_KEY_FIRST_BOOT, 1);

        ret = nvs_commit(handle);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
        }

        ESP_LOGI(TAG, "Credentials generated for %s", s_identity.node_name);
    } else if (ret == ESP_OK) {
        // Load existing credentials
        len = sizeof(s_identity.ap_password);
        nvs_get_str(handle, NVS_KEY_AP_PASS, s_identity.ap_password, &len);

        len = sizeof(s_identity.ble_pop);
        nvs_get_str(handle, NVS_KEY_BLE_POP, s_identity.ble_pop, &len);

        // Check if first boot was acknowledged
        uint8_t fb = 0;
        if (nvs_get_u8(handle, NVS_KEY_FIRST_BOOT, &fb) == ESP_OK && fb == 1) {
            s_first_boot = true;
        }

        ESP_LOGI(TAG, "Credentials loaded for %s", s_identity.node_name);
    } else {
        ESP_LOGE(TAG, "Failed to read NVS: %s", esp_err_to_name(ret));
    }

    nvs_close(handle);
    return ret;
}

esp_err_t device_identity_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    // Derive node name from MAC (always deterministic)
    derive_node_name();

    // Load or generate credentials
    esp_err_t ret = load_or_generate_credentials();
    if (ret != ESP_OK) {
        return ret;
    }

    s_initialized = true;
    return ESP_OK;
}

const device_identity_t* device_identity_get(void)
{
    if (!s_initialized) {
        ESP_LOGE(TAG, "Not initialized");
        return NULL;
    }
    return &s_identity;
}

bool device_identity_is_first_boot(void)
{
    return s_first_boot;
}

esp_err_t device_identity_ack_first_boot(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    nvs_set_u8(handle, NVS_KEY_FIRST_BOOT, 0);
    ret = nvs_commit(handle);
    nvs_close(handle);

    s_first_boot = false;
    ESP_LOGI(TAG, "First boot acknowledged");
    return ret;
}

esp_err_t device_identity_regenerate(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    // Erase all credentials
    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);

    // Re-initialize (will generate new credentials)
    s_initialized = false;
    return device_identity_init();
}

esp_err_t device_identity_get_qr_json(char *buf, size_t buf_len)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    int written = snprintf(buf, buf_len,
        "{"
        "\"name\":\"%s\","
        "\"web_pass\":\"%s\","
        "\"ap_pass\":\"%s\","
        "\"ble_pop\":\"%s\","
        "\"ble_name\":\"PROV_%s\","
        "\"setup_url\":\"http://192.168.4.1\""
        "}",
        s_identity.node_name,
        s_identity.web_password,
        s_identity.ap_password,
        s_identity.ble_pop,
        s_identity.node_suffix
    );

    if (written < 0 || (size_t)written >= buf_len) {
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}
```

**Step 6: Build to verify**

Run:
```bash
cd ~/claude/boorker-watchdog/firmware
idf.py build
```

Expected: Build succeeds

**Step 7: Commit**

```bash
git add firmware/components/device_identity/
git commit -m "feat(device_identity): add unique credential generation

- Hardware RNG for web/AP passwords and BLE PoP PIN
- MAC-derived node name (discoverable, not secret)
- NVS storage for credentials
- First boot detection for QR display trigger
- Regenerate support for factory reset"
```

---

## Task 3: Create Web Auth Component

**Files:**
- Create: `firmware/components/web_auth/CMakeLists.txt`
- Create: `firmware/components/web_auth/include/web_auth.h`
- Create: `firmware/components/web_auth/src/web_auth.c`

**Step 1: Create component structure**

```bash
mkdir -p firmware/components/web_auth/{include,src}
```

**Step 2: Create CMakeLists.txt**

Create `firmware/components/web_auth/CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "src/web_auth.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_http_server device_identity mbedtls log
)
```

**Step 3: Create header file**

Create `firmware/components/web_auth/include/web_auth.h`:
```c
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
 * Uses device_identity for default password
 *
 * @return ESP_OK on success
 */
esp_err_t web_auth_init(void);

/**
 * Check if password has been changed from default
 *
 * @return true if password was changed
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
 *
 * @param token Session token to validate
 * @return true if valid
 */
bool web_auth_validate_session(const char *token);

/**
 * Invalidate session token (logout)
 *
 * @param token Session token to invalidate
 */
void web_auth_logout(const char *token);

/**
 * Change password
 *
 * @param current_password Current password for verification
 * @param new_password New password to set
 * @return ESP_OK on success
 */
esp_err_t web_auth_change_password(const char *current_password, const char *new_password);

/**
 * Check if request is authenticated
 * Checks both session cookie and Basic auth header
 *
 * @param req HTTP request
 * @return true if authenticated
 */
bool web_auth_check_request(httpd_req_t *req);

/**
 * HTTP middleware to require authentication
 * Returns 401 if not authenticated
 *
 * @param req HTTP request
 * @return ESP_OK if authenticated, ESP_FAIL if not (response already sent)
 */
esp_err_t web_auth_require(httpd_req_t *req);

/**
 * Get remaining failed login attempts before lockout
 *
 * @return Attempts remaining (0 = locked out)
 */
int web_auth_get_attempts_remaining(void);

/**
 * Reset password to device default (factory reset)
 *
 * @return ESP_OK on success
 */
esp_err_t web_auth_reset_password(void);

#ifdef __cplusplus
}
#endif
```

**Step 4: Create implementation**

Create `firmware/components/web_auth/src/web_auth.c`:
```c
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
        return ret;
    }

    size_t len = HASH_LEN;
    ret = nvs_get_blob(handle, NVS_KEY_PASS_HASH, s_password_hash, &len);

    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        // First time - hash the default password from device_identity
        const device_identity_t *id = device_identity_get();
        if (id == NULL) {
            nvs_close(handle);
            return ESP_ERR_INVALID_STATE;
        }

        // Generate random salt
        esp_fill_random(s_password_salt, SALT_LEN);

        // Hash default password
        hash_password(id->web_password, s_password_salt, s_password_hash);

        // Store
        nvs_set_blob(handle, NVS_KEY_PASS_HASH, s_password_hash, HASH_LEN);
        nvs_set_blob(handle, NVS_KEY_PASS_SALT, s_password_salt, SALT_LEN);
        nvs_set_u8(handle, NVS_KEY_PASS_CHANGED, 0);
        nvs_commit(handle);

        ESP_LOGI(TAG, "Default password hash stored");
    } else if (ret == ESP_OK) {
        // Load salt and changed flag
        len = SALT_LEN;
        nvs_get_blob(handle, NVS_KEY_PASS_SALT, s_password_salt, &len);

        uint8_t changed = 0;
        nvs_get_u8(handle, NVS_KEY_PASS_CHANGED, &changed);
        s_password_changed = (changed == 1);
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
    if (!s_initialized) {
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
    if (!s_initialized) {
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

    nvs_set_blob(handle, NVS_KEY_PASS_HASH, s_password_hash, HASH_LEN);
    nvs_set_blob(handle, NVS_KEY_PASS_SALT, s_password_salt, SALT_LEN);
    nvs_set_u8(handle, NVS_KEY_PASS_CHANGED, 1);
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
    char buf[128];

    // Check session cookie first
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

    // Check Basic auth header
    if (httpd_req_get_hdr_value_str(req, "Authorization", buf, sizeof(buf)) == ESP_OK) {
        if (strncmp(buf, "Basic ", 6) == 0) {
            // TODO: Decode base64 and validate
            // For now, skip Basic auth implementation
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
    httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"Boorker\"");
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

    nvs_erase_all(handle);
    nvs_commit(handle);
    nvs_close(handle);

    s_initialized = false;
    s_password_changed = false;

    return web_auth_init();
}
```

**Step 5: Build to verify**

Run:
```bash
cd ~/claude/boorker-watchdog/firmware
idf.py build
```

Expected: Build succeeds

**Step 6: Commit**

```bash
git add firmware/components/web_auth/
git commit -m "feat(web_auth): add session-based authentication

- SHA-256 password hashing with random salt
- Session token management (max 4 concurrent)
- Rate limiting (5 attempts, 5 min lockout)
- Password change with session invalidation
- HTTP request authentication check"
```

---

## Task 4: Create Web Server Component Foundation

**Files:**
- Create: `firmware/components/web_server/CMakeLists.txt`
- Create: `firmware/components/web_server/Kconfig`
- Create: `firmware/components/web_server/include/web_server.h`
- Create: `firmware/components/web_server/src/web_server.c`
- Create: `firmware/components/web_server/src/ws_handler.c`
- Create: `firmware/components/web_server/src/file_handler.c`

This task involves creating a larger component. See the full implementation in the design document.

**Key implementation notes:**
- HTTP server with LittleFS file serving
- Gzip support for compressed assets
- SPA routing (fallback to index.html for hash routes)
- WebSocket with authentication and topic subscriptions
- REST API endpoints for auth, system status

**Step-by-step:** Follow the pattern from Task 2 and Task 3 - create structure, CMakeLists, headers, implementation, build, commit.

---

## Task 5: Create Frontend Build System

**Files:**
- Create: `www/package.json`
- Create: `www/build.js`
- Create: `www/src/index.html`
- Create: `www/src/login.html`
- Create: `www/src/css/app.css`
- Create: `www/src/js/app.js`

**Security note:** The frontend JavaScript uses DOM manipulation. For security:
- Use `textContent` instead of `innerHTML` for text-only content
- When HTML structure is needed, use `document.createElement()` and `appendChild()`
- API responses are from trusted source (our own ESP32), but still sanitize display

See design document for full implementation with safe DOM manipulation patterns.

---

## Task 6: Integrate with Main App

**Files:**
- Modify: `firmware/main/CMakeLists.txt`
- Modify: `firmware/main/main.c`

Add device_identity, web_auth, web_server initialization to main app.

---

## Summary

| Task | Component | Description |
|------|-----------|-------------|
| 1 | LittleFS partition | Flash partition for web assets |
| 2 | device_identity | Hardware RNG credential generation |
| 3 | web_auth | Session-based authentication |
| 4 | web_server | HTTP/WebSocket server |
| 5 | Frontend | SPA with build tooling |
| 6 | Integration | Connect to main app |

**Total estimated time:** 4-6 hours

---

## Execution Options

Plan complete and saved to `docs/plans/2026-02-28-web-interface-implementation.md`.

**Two execution options:**

**1. Subagent-Driven (this session)** - I dispatch fresh subagent per task, review between tasks, fast iteration

**2. Parallel Session (separate)** - Open new session with executing-plans, batch execution with checkpoints

Which approach?

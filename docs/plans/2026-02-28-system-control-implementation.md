# System Control Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Add console commands (reboot, version, free, uptime, status) and REST API endpoints for system management.

**Architecture:** New `system_console` component provides console commands and shared reboot logic. Web server gains new API endpoints. Device-agnostic design detects PSRAM at runtime.

**Tech Stack:** ESP-IDF esp_console, argtable3, FreeRTOS timers, cJSON

---

## Task 1: Create system_console Component Scaffold

**Files:**
- Create: `firmware/components/system_console/CMakeLists.txt`
- Create: `firmware/components/system_console/Kconfig`
- Create: `firmware/components/system_console/include/system_console.h`
- Create: `firmware/components/system_console/src/system_console.c`

**Step 1: Create CMakeLists.txt**

```cmake
idf_component_register(
    SRCS "src/system_console.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_console argtable3 esp_timer device_identity wifi_manager tailscale_manager
)
```

**Step 2: Create Kconfig**

```kconfig
menu "System Console"

    config SYSTEM_CONSOLE_REBOOT_DEFAULT_DELAY
        int "Default reboot delay (seconds)"
        default 3
        range 0 300
        help
            Default delay before reboot when no argument provided.

    config SYSTEM_CONSOLE_REBOOT_MAX_DELAY
        int "Maximum reboot delay (seconds)"
        default 300
        range 10 3600
        help
            Maximum allowed delay for scheduled reboot.

endmenu
```

**Step 3: Create header file**

```c
#pragma once

#include "esp_err.h"
#include <stdint.h>
#include <stdbool.h>

/**
 * @brief Register all system console commands
 *
 * Commands: reboot, version, free, uptime, status
 */
esp_err_t system_console_register(void);

/**
 * @brief Schedule a system reboot
 *
 * @param delay_seconds Seconds until reboot (0 = immediate)
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if reboot already pending
 */
esp_err_t system_reboot_schedule(uint32_t delay_seconds);

/**
 * @brief Cancel a pending reboot
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if no reboot pending
 */
esp_err_t system_reboot_cancel(void);

/**
 * @brief Check if a reboot is pending
 */
bool system_reboot_is_pending(void);

/**
 * @brief Get seconds remaining until reboot
 *
 * @return Seconds remaining, or 0 if no reboot pending
 */
uint32_t system_reboot_get_remaining(void);
```

**Step 4: Create minimal implementation stub**

```c
#include "system_console.h"
#include "esp_log.h"

static const char *TAG = "sys_console";

esp_err_t system_console_register(void)
{
    ESP_LOGI(TAG, "System console commands registered");
    return ESP_OK;
}

esp_err_t system_reboot_schedule(uint32_t delay_seconds)
{
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t system_reboot_cancel(void)
{
    return ESP_ERR_INVALID_STATE;
}

bool system_reboot_is_pending(void)
{
    return false;
}

uint32_t system_reboot_get_remaining(void)
{
    return 0;
}
```

**Step 5: Verify build**

```bash
cd /home/codyreed/claude/boorker-watchdog/firmware && idf.py build
```

Expected: Build succeeds (component discovered but not yet linked)

**Step 6: Commit**

```bash
git add components/system_console/
git commit -m "feat(system_console): add component scaffold

Stub implementation for console commands and reboot scheduling API.

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 2: Implement Reboot Command with Timer

**Files:**
- Modify: `firmware/components/system_console/src/system_console.c`

**Step 1: Add includes and static variables**

Add at top of file after existing includes:

```c
#include "system_console.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "argtable3/argtable3.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "sys_console";

// Reboot state
static esp_timer_handle_t s_reboot_timer = NULL;
static uint32_t s_reboot_scheduled_time = 0;  // Time when reboot was scheduled (us)
static uint32_t s_reboot_delay_us = 0;        // Total delay in microseconds
```

**Step 2: Implement reboot timer callback and scheduling functions**

Replace the stub implementations:

```c
static void reboot_timer_callback(void *arg)
{
    printf("Restarting now.\n");
    esp_restart();
}

esp_err_t system_reboot_schedule(uint32_t delay_seconds)
{
    if (s_reboot_timer != NULL) {
        return ESP_ERR_INVALID_STATE;  // Already pending
    }

    if (delay_seconds > CONFIG_SYSTEM_CONSOLE_REBOOT_MAX_DELAY) {
        return ESP_ERR_INVALID_ARG;
    }

    if (delay_seconds == 0) {
        printf("Restarting now.\n");
        esp_restart();
        return ESP_OK;  // Won't reach here
    }

    // Create one-shot timer
    esp_timer_create_args_t timer_args = {
        .callback = reboot_timer_callback,
        .name = "reboot_timer"
    };

    esp_err_t ret = esp_timer_create(&timer_args, &s_reboot_timer);
    if (ret != ESP_OK) {
        return ret;
    }

    s_reboot_delay_us = delay_seconds * 1000000ULL;
    s_reboot_scheduled_time = esp_timer_get_time();

    ret = esp_timer_start_once(s_reboot_timer, s_reboot_delay_us);
    if (ret != ESP_OK) {
        esp_timer_delete(s_reboot_timer);
        s_reboot_timer = NULL;
        return ret;
    }

    return ESP_OK;
}

esp_err_t system_reboot_cancel(void)
{
    if (s_reboot_timer == NULL) {
        return ESP_ERR_INVALID_STATE;  // No reboot pending
    }

    esp_timer_stop(s_reboot_timer);
    esp_timer_delete(s_reboot_timer);
    s_reboot_timer = NULL;
    s_reboot_scheduled_time = 0;
    s_reboot_delay_us = 0;

    return ESP_OK;
}

bool system_reboot_is_pending(void)
{
    return s_reboot_timer != NULL;
}

uint32_t system_reboot_get_remaining(void)
{
    if (s_reboot_timer == NULL) {
        return 0;
    }

    uint64_t elapsed = esp_timer_get_time() - s_reboot_scheduled_time;
    if (elapsed >= s_reboot_delay_us) {
        return 0;
    }

    return (s_reboot_delay_us - elapsed) / 1000000;
}
```

**Step 3: Implement reboot console command**

Add before `system_console_register`:

```c
// Reboot command arguments
static struct {
    struct arg_str *action;
    struct arg_end *end;
} reboot_args;

static int cmd_reboot(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&reboot_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, reboot_args.end, argv[0]);
        return 1;
    }

    const char *action = reboot_args.action->count > 0 ? reboot_args.action->sval[0] : NULL;

    // Handle cancel
    if (action && strcmp(action, "cancel") == 0) {
        if (system_reboot_cancel() == ESP_OK) {
            printf("Reboot cancelled.\n");
            return 0;
        } else {
            printf("No reboot pending.\n");
            return 1;
        }
    }

    // Check if reboot already pending
    if (system_reboot_is_pending()) {
        printf("Reboot already scheduled (%lu seconds remaining). Use 'reboot cancel' to abort.\n",
               (unsigned long)system_reboot_get_remaining());
        return 1;
    }

    // Determine delay
    uint32_t delay = CONFIG_SYSTEM_CONSOLE_REBOOT_DEFAULT_DELAY;

    if (action) {
        if (strcmp(action, "now") == 0) {
            delay = 0;
        } else {
            // Try to parse as number
            char *endptr;
            long val = strtol(action, &endptr, 10);
            if (*endptr == '\0' && val >= 0 && val <= CONFIG_SYSTEM_CONSOLE_REBOOT_MAX_DELAY) {
                delay = (uint32_t)val;
            } else {
                printf("Invalid argument. Usage: reboot [now|<seconds>|cancel]\n");
                return 1;
            }
        }
    }

    // Schedule reboot
    esp_err_t ret = system_reboot_schedule(delay);
    if (ret != ESP_OK) {
        printf("Failed to schedule reboot: %s\n", esp_err_to_name(ret));
        return 1;
    }

    if (delay > 0) {
        printf("Reboot scheduled in %lu seconds. Use 'reboot cancel' to abort.\n", (unsigned long)delay);
    }

    return 0;
}
```

**Step 4: Register reboot command in system_console_register**

Replace the stub `system_console_register`:

```c
esp_err_t system_console_register(void)
{
    // Reboot command
    reboot_args.action = arg_str0(NULL, NULL, "[now|seconds|cancel]", "Reboot action");
    reboot_args.end = arg_end(1);

    const esp_console_cmd_t reboot_cmd = {
        .command = "reboot",
        .help = "Reboot device (default: 3s delay)",
        .hint = NULL,
        .func = &cmd_reboot,
        .argtable = &reboot_args,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&reboot_cmd));

    ESP_LOGI(TAG, "Registered command: reboot");
    return ESP_OK;
}
```

**Step 5: Build and verify**

```bash
cd /home/codyreed/claude/boorker-watchdog/firmware && idf.py build
```

Expected: Build succeeds

**Step 6: Commit**

```bash
git add -A
git commit -m "feat(system_console): implement reboot command with timer

- Schedule reboot with configurable delay (default 3s)
- Cancel pending reboot with 'reboot cancel'
- Immediate reboot with 'reboot now'
- Uses FreeRTOS one-shot timer

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 3: Implement version, free, uptime Commands

**Files:**
- Modify: `firmware/components/system_console/src/system_console.c`

**Step 1: Add required includes**

Add near top with other includes:

```c
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_heap_caps.h"
#include "version.h"
```

**Step 2: Implement version command**

Add before `system_console_register`:

```c
static int cmd_version(int argc, char **argv)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    printf("Boorker v%s\n", BOORKER_VERSION_STRING);
    printf("ESP-IDF %s\n", esp_get_idf_version());
    printf("Chip: ESP32-S3 rev%d, %d cores\n", chip_info.revision, chip_info.cores);

    return 0;
}
```

**Step 3: Implement free command with PSRAM detection**

```c
static int cmd_free(int argc, char **argv)
{
    printf("Memory:\n");
    printf("  Heap:  %lu bytes free (min: %lu)\n",
           (unsigned long)esp_get_free_heap_size(),
           (unsigned long)esp_get_minimum_free_heap_size());

    // Check for PSRAM - returns 0 if not available
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (psram_free > 0) {
        printf("  PSRAM: %lu bytes free\n", (unsigned long)psram_free);
    }

    return 0;
}
```

**Step 4: Implement uptime command**

```c
static int cmd_uptime(int argc, char **argv)
{
    int64_t uptime_us = esp_timer_get_time();
    uint32_t uptime_sec = uptime_us / 1000000;

    uint32_t hours = uptime_sec / 3600;
    uint32_t minutes = (uptime_sec % 3600) / 60;
    uint32_t seconds = uptime_sec % 60;

    printf("Uptime: %luh %lum %lus\n",
           (unsigned long)hours, (unsigned long)minutes, (unsigned long)seconds);

    return 0;
}
```

**Step 5: Register commands in system_console_register**

Add after reboot command registration:

```c
    // Version command
    const esp_console_cmd_t version_cmd = {
        .command = "version",
        .help = "Show firmware version",
        .hint = NULL,
        .func = &cmd_version,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&version_cmd));

    // Free command
    const esp_console_cmd_t free_cmd = {
        .command = "free",
        .help = "Show memory statistics",
        .hint = NULL,
        .func = &cmd_free,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&free_cmd));

    // Uptime command
    const esp_console_cmd_t uptime_cmd = {
        .command = "uptime",
        .help = "Show system uptime",
        .hint = NULL,
        .func = &cmd_uptime,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&uptime_cmd));

    ESP_LOGI(TAG, "Registered commands: reboot, version, free, uptime");
```

**Step 6: Build**

```bash
cd /home/codyreed/claude/boorker-watchdog/firmware && idf.py build
```

**Step 7: Commit**

```bash
git add -A
git commit -m "feat(system_console): add version, free, uptime commands

- version: shows firmware, IDF version, chip info
- free: shows heap and PSRAM (if available)
- uptime: shows formatted system uptime

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 4: Implement status Command

**Files:**
- Modify: `firmware/components/system_console/src/system_console.c`

**Step 1: Add wifi and tailscale includes**

Add with other includes:

```c
#include "wifi_manager.h"
#include "tailscale_manager.h"
#include "device_identity.h"
```

**Step 2: Implement status command**

Add before `system_console_register`:

```c
static int cmd_status(int argc, char **argv)
{
    // Header with version and node name
    const device_identity_t *id = device_identity_get();
    printf("Boorker v%s", BOORKER_VERSION_STRING);
    if (id) {
        printf(" - %s", id->node_name);
    }
    printf("\n");

    // Uptime
    int64_t uptime_us = esp_timer_get_time();
    uint32_t uptime_sec = uptime_us / 1000000;
    uint32_t hours = uptime_sec / 3600;
    uint32_t minutes = (uptime_sec % 3600) / 60;
    uint32_t seconds = uptime_sec % 60;
    printf("Uptime: %luh %lum %lus\n",
           (unsigned long)hours, (unsigned long)minutes, (unsigned long)seconds);

    // Memory
    printf("Memory:\n");
    printf("  Heap:  %lu bytes free\n", (unsigned long)esp_get_free_heap_size());
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (psram_free > 0) {
        printf("  PSRAM: %lu bytes free\n", (unsigned long)psram_free);
    }

    // WiFi status
    char ip[16] = {0};
    wifi_mgr_get_ip(ip, sizeof(ip));
    printf("WiFi: %s", wifi_mgr_get_state_name());
    if (ip[0] != '\0') {
        printf(" (%s)", ip);
    }
    printf("\n");

    // Tailscale status
    char ts_ip[16] = {0};
    ts_mgr_get_ip(ts_ip, sizeof(ts_ip));
    printf("Tailscale: %s", ts_mgr_get_state_name());
    if (ts_ip[0] != '\0') {
        printf(" (%s)", ts_ip);
    }
    printf("\n");

    return 0;
}
```

**Step 3: Register status command**

Add in `system_console_register` after uptime:

```c
    // Status command
    const esp_console_cmd_t status_cmd = {
        .command = "status",
        .help = "Show system status overview",
        .hint = NULL,
        .func = &cmd_status,
    };
    ESP_ERROR_CHECK(esp_console_cmd_register(&status_cmd));

    ESP_LOGI(TAG, "Registered commands: reboot, version, free, uptime, status");
```

**Step 4: Build**

```bash
cd /home/codyreed/claude/boorker-watchdog/firmware && idf.py build
```

**Step 5: Commit**

```bash
git add -A
git commit -m "feat(system_console): add status command

Combined overview showing version, uptime, memory, WiFi, and Tailscale status.

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 5: Integrate system_console into main.c

**Files:**
- Modify: `firmware/main/main.c`

**Step 1: Add include**

Add after other component includes:

```c
#include "system_console.h"
```

**Step 2: Register system console commands**

In `init_console()` function, add after `ts_console_register()`:

```c
    // Register system console commands
    system_console_register();
```

**Step 3: Build**

```bash
cd /home/codyreed/claude/boorker-watchdog/firmware && idf.py build
```

**Step 4: Commit**

```bash
git add -A
git commit -m "feat(main): integrate system_console commands

Register reboot, version, free, uptime, status commands at startup.

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 6: Add web_auth Password Change Support

**Files:**
- Modify: `firmware/components/web_auth/include/web_auth.h`
- Modify: `firmware/components/web_auth/src/web_auth.c`

**Step 1: Add function declaration to header**

Add in `web_auth.h`:

```c
/**
 * @brief Change the web password
 *
 * @param current_password Current password for verification
 * @param new_password New password to set
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_ARG if current password incorrect
 *         ESP_FAIL on storage error
 */
esp_err_t web_auth_change_password(const char *current_password, const char *new_password);

/**
 * @brief Invalidate all active sessions
 */
void web_auth_invalidate_all_sessions(void);
```

**Step 2: Implement in web_auth.c**

Add implementations:

```c
esp_err_t web_auth_change_password(const char *current_password, const char *new_password)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (current_password == NULL || new_password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    // Verify current password
    if (!verify_password(current_password)) {
        return ESP_ERR_INVALID_ARG;
    }

    // Hash and store new password
    uint8_t salt[WEB_AUTH_SALT_LEN];
    esp_fill_random(salt, sizeof(salt));

    uint8_t hash[32];
    hash_password(new_password, salt, hash);

    // Store in NVS
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        return ret;
    }

    ret = nvs_set_blob(handle, NVS_KEY_PASS_HASH, hash, sizeof(hash));
    if (ret != ESP_OK) {
        nvs_close(handle);
        return ret;
    }

    ret = nvs_set_blob(handle, NVS_KEY_PASS_SALT, salt, sizeof(salt));
    if (ret != ESP_OK) {
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    nvs_close(handle);

    if (ret == ESP_OK) {
        // Update in-memory copy
        memcpy(s_password_hash, hash, sizeof(hash));
        memcpy(s_password_salt, salt, sizeof(salt));

        // Invalidate all sessions
        web_auth_invalidate_all_sessions();

        ESP_LOGI(TAG, "Password changed successfully");
    }

    return ret;
}

void web_auth_invalidate_all_sessions(void)
{
    memset(s_sessions, 0, sizeof(s_sessions));
    ESP_LOGI(TAG, "All sessions invalidated");
}
```

**Step 3: Build**

```bash
cd /home/codyreed/claude/boorker-watchdog/firmware && idf.py build
```

**Step 4: Commit**

```bash
git add -A
git commit -m "feat(web_auth): add password change and session invalidation

- web_auth_change_password() verifies current before setting new
- web_auth_invalidate_all_sessions() clears all active sessions

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 7: Add API Endpoints - Auth Status and Password

**Files:**
- Modify: `firmware/components/web_server/src/web_server.c`

**Step 1: Add auth/status handler**

Add after existing API handlers:

```c
// GET /api/v1/auth/status
static esp_err_t api_auth_status(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/json");

    // Check if authenticated (don't use web_auth_require - we want to return status, not 401)
    char token[WEB_AUTH_SESSION_TOKEN_LEN + 1] = {0};
    char *cookie = NULL;
    size_t cookie_len = httpd_req_get_hdr_value_len(req, "Cookie");

    bool authenticated = false;

    if (cookie_len > 0 && cookie_len < 512) {
        cookie = malloc(cookie_len + 1);
        if (cookie && httpd_req_get_hdr_value_str(req, "Cookie", cookie, cookie_len + 1) == ESP_OK) {
            // Parse session token from cookie
            char *session = strstr(cookie, "session=");
            if (session) {
                session += 8;  // Skip "session="
                char *end = strchr(session, ';');
                size_t len = end ? (size_t)(end - session) : strlen(session);
                if (len == WEB_AUTH_SESSION_TOKEN_LEN) {
                    memcpy(token, session, len);
                    token[len] = '\0';
                    authenticated = web_auth_validate_session(token);
                }
            }
        }
        free(cookie);
    }

    if (authenticated) {
        httpd_resp_sendstr(req, "{\"authenticated\":true,\"username\":\"admin\"}");
    } else {
        httpd_resp_sendstr(req, "{\"authenticated\":false}");
    }

    return ESP_OK;
}
```

**Step 2: Add auth/password handler**

```c
// PUT /api/v1/auth/password
static esp_err_t api_auth_password(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK;
    }

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

    cJSON *current = cJSON_GetObjectItem(json, "current_password");
    cJSON *new_pass = cJSON_GetObjectItem(json, "new_password");

    httpd_resp_set_type(req, "application/json");

    if (!cJSON_IsString(current) || !cJSON_IsString(new_pass)) {
        cJSON_Delete(json);
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Missing required fields\"}");
        return ESP_OK;
    }

    esp_err_t err = web_auth_change_password(current->valuestring, new_pass->valuestring);
    cJSON_Delete(json);

    if (err == ESP_OK) {
        // Clear the session cookie
        httpd_resp_set_hdr(req, "Set-Cookie", "session=; Path=/; Max-Age=0; SameSite=Strict");
        httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Password changed. Please log in again.\"}");
    } else if (err == ESP_ERR_INVALID_ARG) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Current password incorrect\"}");
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Failed to update password\"}");
    }

    return ESP_OK;
}
```

**Step 3: Register endpoints in web_server_start**

Add after existing auth endpoints:

```c
    httpd_uri_t uri_auth_status = {
        .uri = "/api/v1/auth/status",
        .method = HTTP_GET,
        .handler = api_auth_status,
    };
    httpd_register_uri_handler(s_server, &uri_auth_status);

    httpd_uri_t uri_auth_password = {
        .uri = "/api/v1/auth/password",
        .method = HTTP_PUT,
        .handler = api_auth_password,
    };
    httpd_register_uri_handler(s_server, &uri_auth_password);
```

**Step 4: Build**

```bash
cd /home/codyreed/claude/boorker-watchdog/firmware && idf.py build
```

**Step 5: Commit**

```bash
git add -A
git commit -m "feat(web_server): add auth/status and auth/password endpoints

- GET /api/v1/auth/status returns authentication state
- PUT /api/v1/auth/password changes password (requires current)

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 8: Add API Endpoints - System Reboot and Factory Reset

**Files:**
- Modify: `firmware/components/web_server/src/web_server.c`

**Step 1: Add system_console include**

Add with other includes:

```c
#include "system_console.h"
```

**Step 2: Add system/reboot POST handler**

```c
// POST /api/v1/system/reboot
static esp_err_t api_system_reboot_post(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK;
    }

    uint32_t delay = 3;  // Default

    // Parse optional body
    char buf[64];
    int ret = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (ret > 0) {
        buf[ret] = '\0';
        cJSON *json = cJSON_Parse(buf);
        if (json) {
            cJSON *delay_item = cJSON_GetObjectItem(json, "delay");
            if (cJSON_IsNumber(delay_item)) {
                int d = delay_item->valueint;
                if (d >= 0 && d <= CONFIG_SYSTEM_CONSOLE_REBOOT_MAX_DELAY) {
                    delay = (uint32_t)d;
                }
            }
            cJSON_Delete(json);
        }
    }

    httpd_resp_set_type(req, "application/json");

    esp_err_t err = system_reboot_schedule(delay);
    if (err == ESP_OK) {
        char resp[128];
        snprintf(resp, sizeof(resp),
                 "{\"success\":true,\"message\":\"Rebooting in %lu seconds\",\"delay\":%lu}",
                 (unsigned long)delay, (unsigned long)delay);
        httpd_resp_sendstr(req, resp);
    } else if (err == ESP_ERR_INVALID_STATE) {
        httpd_resp_set_status(req, "409 Conflict");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Reboot already pending\"}");
    } else {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Failed to schedule reboot\"}");
    }

    return ESP_OK;
}

// DELETE /api/v1/system/reboot
static esp_err_t api_system_reboot_delete(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");

    esp_err_t err = system_reboot_cancel();
    if (err == ESP_OK) {
        httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Reboot cancelled\"}");
    } else {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"No reboot pending\"}");
    }

    return ESP_OK;
}
```

**Step 3: Add factory-reset handler**

```c
// POST /api/v1/system/factory-reset
static esp_err_t api_system_factory_reset(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK;
    }

    char buf[64];
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

    cJSON *confirm = cJSON_GetObjectItem(json, "confirm");
    bool confirmed = cJSON_IsTrue(confirm);
    cJSON_Delete(json);

    httpd_resp_set_type(req, "application/json");

    if (!confirmed) {
        httpd_resp_set_status(req, "400 Bad Request");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Confirmation required\"}");
        return ESP_OK;
    }

    // Regenerate credentials
    esp_err_t err = device_identity_regenerate();
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Failed to reset credentials\"}");
        return ESP_OK;
    }

    httpd_resp_sendstr(req, "{\"success\":true,\"message\":\"Factory reset initiated. Device will reboot with new credentials.\"}");

    // Schedule reboot after response sent
    system_reboot_schedule(2);

    return ESP_OK;
}
```

**Step 4: Register endpoints**

Add in `web_server_start`:

```c
    httpd_uri_t uri_reboot_post = {
        .uri = "/api/v1/system/reboot",
        .method = HTTP_POST,
        .handler = api_system_reboot_post,
    };
    httpd_register_uri_handler(s_server, &uri_reboot_post);

    httpd_uri_t uri_reboot_delete = {
        .uri = "/api/v1/system/reboot",
        .method = HTTP_DELETE,
        .handler = api_system_reboot_delete,
    };
    httpd_register_uri_handler(s_server, &uri_reboot_delete);

    httpd_uri_t uri_factory_reset = {
        .uri = "/api/v1/system/factory-reset",
        .method = HTTP_POST,
        .handler = api_system_factory_reset,
    };
    httpd_register_uri_handler(s_server, &uri_factory_reset);
```

**Step 5: Build**

```bash
cd /home/codyreed/claude/boorker-watchdog/firmware && idf.py build
```

**Step 6: Commit**

```bash
git add -A
git commit -m "feat(web_server): add reboot and factory-reset endpoints

- POST /api/v1/system/reboot schedules reboot with delay
- DELETE /api/v1/system/reboot cancels pending reboot
- POST /api/v1/system/factory-reset regenerates credentials

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 9: Add API Endpoint - System QR and Enhance Existing Endpoints

**Files:**
- Modify: `firmware/components/web_server/src/web_server.c`

**Step 1: Add system/qr handler**

```c
// GET /api/v1/system/qr
static esp_err_t api_system_qr(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK;
    }

    httpd_resp_set_type(req, "application/json");

    // Only allow if first boot not yet acknowledged
    if (!device_identity_is_first_boot()) {
        httpd_resp_set_status(req, "403 Forbidden");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Credentials already acknowledged\"}");
        return ESP_OK;
    }

    char qr_json[256];
    esp_err_t err = device_identity_get_qr_json(qr_json, sizeof(qr_json));
    if (err != ESP_OK) {
        httpd_resp_set_status(req, "500 Internal Server Error");
        httpd_resp_sendstr(req, "{\"error\":true,\"message\":\"Failed to generate QR data\"}");
        return ESP_OK;
    }

    httpd_resp_sendstr(req, qr_json);
    return ESP_OK;
}
```

**Step 2: Enhance system/status handler**

Replace existing `api_system_status`:

```c
// GET /api/v1/system/status
static esp_err_t api_system_status(httpd_req_t *req)
{
    if (web_auth_require(req) != ESP_OK) {
        return ESP_OK;
    }

    cJSON *json = cJSON_CreateObject();
    cJSON_AddNumberToObject(json, "uptime", esp_timer_get_time() / 1000000);
    cJSON_AddNumberToObject(json, "heap_free", esp_get_free_heap_size());
    cJSON_AddNumberToObject(json, "heap_min", esp_get_minimum_free_heap_size());

    // Add PSRAM if available
    size_t psram_free = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (psram_free > 0) {
        cJSON_AddNumberToObject(json, "psram_free", psram_free);
    }

    const device_identity_t *id = device_identity_get();
    if (id) {
        cJSON_AddStringToObject(json, "node_name", id->node_name);
    }

    // Power stub
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
```

**Step 3: Enhance system/info handler**

Replace existing `api_system_info`:

```c
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

    // Add version info
    cJSON_AddStringToObject(json, "version", BOORKER_VERSION_STRING);
    cJSON_AddStringToObject(json, "idf_version", esp_get_idf_version());

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
```

**Step 4: Add required includes**

Ensure these are at top of file:

```c
#include "esp_idf_version.h"
#include "esp_heap_caps.h"
#include "version.h"
```

**Step 5: Register QR endpoint**

Add in `web_server_start`:

```c
    httpd_uri_t uri_qr = {
        .uri = "/api/v1/system/qr",
        .method = HTTP_GET,
        .handler = api_system_qr,
    };
    httpd_register_uri_handler(s_server, &uri_qr);
```

**Step 6: Build**

```bash
cd /home/codyreed/claude/boorker-watchdog/firmware && idf.py build
```

**Step 7: Commit**

```bash
git add -A
git commit -m "feat(web_server): add QR endpoint, enhance status/info

- GET /api/v1/system/qr returns provisioning data (first boot only)
- system/status now includes heap_min and psram_free (if available)
- system/info now includes version and idf_version

Co-Authored-By: Claude Opus 4.6 <noreply@anthropic.com>"
```

---

## Task 10: Flash and Manual Test

**Step 1: Full build**

```bash
cd /home/codyreed/claude/boorker-watchdog/firmware && idf.py build
```

**Step 2: Flash device**

```bash
idf.py -p /dev/ttyUSB0 flash
```

**Step 3: Monitor and test console commands**

```bash
idf.py -p /dev/ttyUSB0 monitor
```

Test commands:
- `help` - verify new commands listed
- `version` - verify output
- `free` - verify memory stats (PSRAM if applicable)
- `uptime` - verify formatted output
- `status` - verify combined output
- `reboot 5` then `reboot cancel` - verify cancellation
- `reboot now` - verify immediate reboot

**Step 4: Test API endpoints via curl**

```bash
# Login
curl -c cookies.txt -X POST http://192.168.68.53/api/v1/auth/login \
  -H "Content-Type: application/json" \
  -d '{"username":"admin","password":"YOUR_PASSWORD"}'

# Auth status
curl -b cookies.txt http://192.168.68.53/api/v1/auth/status

# System status
curl -b cookies.txt http://192.168.68.53/api/v1/system/status

# System info
curl -b cookies.txt http://192.168.68.53/api/v1/system/info

# Schedule reboot (then cancel)
curl -b cookies.txt -X POST http://192.168.68.53/api/v1/system/reboot \
  -H "Content-Type: application/json" \
  -d '{"delay":10}'

curl -b cookies.txt -X DELETE http://192.168.68.53/api/v1/system/reboot
```

**Step 5: Commit test results (if any fixes needed)**

---

## Summary

| Task | Description | Estimated Time |
|------|-------------|----------------|
| 1 | Component scaffold | 5 min |
| 2 | Reboot command + timer | 15 min |
| 3 | version, free, uptime commands | 10 min |
| 4 | status command | 10 min |
| 5 | Integrate into main.c | 5 min |
| 6 | web_auth password change | 10 min |
| 7 | API: auth/status, auth/password | 15 min |
| 8 | API: reboot, factory-reset | 15 min |
| 9 | API: QR, enhance status/info | 10 min |
| 10 | Flash and test | 15 min |

**Total: ~110 minutes**

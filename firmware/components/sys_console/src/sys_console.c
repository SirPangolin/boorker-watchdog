#include "sys_console.h"
#include "esp_log.h"
#include "esp_console.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_chip_info.h"
#include "esp_idf_version.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "argtable3/argtable3.h"
#include "version.h"
#include "credentials.h"
#include "wifi_manager.h"
#include "event_bus.h"
#if CONFIG_TS_MGR_ENABLED
#include "tailscale_manager.h"
#endif
#include <string.h>
#include <stdio.h>

static const char *TAG = "sys_console";

// Mutex for thread-safe access to reboot state
static SemaphoreHandle_t s_reboot_mutex = NULL;

// Reboot state (protected by s_reboot_mutex)
static esp_timer_handle_t s_reboot_timer = NULL;
static uint64_t s_reboot_scheduled_time = 0;  // Time when reboot was scheduled (us)
static uint64_t s_reboot_delay_us = 0;        // Total delay in microseconds

static void reboot_timer_callback(void *arg)
{
    // Check if reboot was cancelled (race condition protection)
    if (s_reboot_mutex != NULL) {
        xSemaphoreTake(s_reboot_mutex, portMAX_DELAY);
        if (s_reboot_timer == NULL) {
            // Reboot was cancelled between timer firing and callback execution
            xSemaphoreGive(s_reboot_mutex);
            ESP_LOGI(TAG, "Reboot callback aborted - cancel was requested");
            return;
        }
        xSemaphoreGive(s_reboot_mutex);
    }
    printf("Restarting now.\n");
    esp_restart();
}

esp_err_t system_reboot_schedule(uint32_t delay_seconds)
{
    if (s_reboot_mutex == NULL) {
        ESP_LOGE(TAG, "sys_console not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_reboot_mutex, portMAX_DELAY);

    if (s_reboot_timer != NULL) {
        xSemaphoreGive(s_reboot_mutex);
        ESP_LOGD(TAG, "Reboot already pending, cannot schedule another");
        return ESP_ERR_INVALID_STATE;
    }

    if (delay_seconds > CONFIG_SYS_CONSOLE_REBOOT_MAX_DELAY) {
        xSemaphoreGive(s_reboot_mutex);
        ESP_LOGW(TAG, "Reboot delay %lu exceeds max %d seconds",
                 (unsigned long)delay_seconds, CONFIG_SYS_CONSOLE_REBOOT_MAX_DELAY);
        return ESP_ERR_INVALID_ARG;
    }

    if (delay_seconds == 0) {
        xSemaphoreGive(s_reboot_mutex);
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
        xSemaphoreGive(s_reboot_mutex);
        ESP_LOGE(TAG, "Failed to create reboot timer: %s", esp_err_to_name(ret));
        return ret;
    }

    s_reboot_delay_us = (uint64_t)delay_seconds * 1000000ULL;
    s_reboot_scheduled_time = esp_timer_get_time();

    ret = esp_timer_start_once(s_reboot_timer, s_reboot_delay_us);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start reboot timer: %s", esp_err_to_name(ret));
        esp_err_t del_ret = esp_timer_delete(s_reboot_timer);
        if (del_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to cleanup timer after start failure: %s", esp_err_to_name(del_ret));
        }
        s_reboot_timer = NULL;
        xSemaphoreGive(s_reboot_mutex);
        return ret;
    }

    xSemaphoreGive(s_reboot_mutex);
    ESP_LOGI(TAG, "Reboot scheduled in %lu seconds", (unsigned long)delay_seconds);
    return ESP_OK;
}

esp_err_t system_reboot_cancel(void)
{
    if (s_reboot_mutex == NULL) {
        ESP_LOGE(TAG, "sys_console not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_reboot_mutex, portMAX_DELAY);

    if (s_reboot_timer == NULL) {
        xSemaphoreGive(s_reboot_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    // Capture timer handle and clear state first to prevent callback issues
    esp_timer_handle_t timer = s_reboot_timer;
    s_reboot_timer = NULL;
    s_reboot_scheduled_time = 0;
    s_reboot_delay_us = 0;

    xSemaphoreGive(s_reboot_mutex);

    // Stop and delete timer outside mutex (safe since we cleared the handle)
    esp_err_t stop_ret = esp_timer_stop(timer);
    if (stop_ret != ESP_OK && stop_ret != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE is expected if timer already fired
        ESP_LOGW(TAG, "Failed to stop reboot timer: %s", esp_err_to_name(stop_ret));
    }

    esp_err_t del_ret = esp_timer_delete(timer);
    if (del_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to delete reboot timer: %s", esp_err_to_name(del_ret));
        return del_ret;
    }

    ESP_LOGI(TAG, "Reboot cancelled");
    return ESP_OK;
}

bool system_reboot_is_pending(void)
{
    if (s_reboot_mutex == NULL) {
        return false;
    }

    xSemaphoreTake(s_reboot_mutex, portMAX_DELAY);
    bool pending = (s_reboot_timer != NULL);
    xSemaphoreGive(s_reboot_mutex);

    return pending;
}

uint32_t system_reboot_get_remaining(void)
{
    if (s_reboot_mutex == NULL) {
        return 0;
    }

    xSemaphoreTake(s_reboot_mutex, portMAX_DELAY);

    if (s_reboot_timer == NULL) {
        xSemaphoreGive(s_reboot_mutex);
        return 0;
    }

    uint64_t elapsed = esp_timer_get_time() - s_reboot_scheduled_time;
    uint32_t remaining = 0;
    if (elapsed < s_reboot_delay_us) {
        remaining = (uint32_t)((s_reboot_delay_us - elapsed) / 1000000);
    }

    xSemaphoreGive(s_reboot_mutex);
    return remaining;
}

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
    uint32_t delay = CONFIG_SYS_CONSOLE_REBOOT_DEFAULT_DELAY;

    if (action) {
        if (strcmp(action, "now") == 0) {
            delay = 0;
        } else {
            // Try to parse as number
            char *endptr;
            long val = strtol(action, &endptr, 10);
            if (*endptr == '\0' && val >= 0 && val <= CONFIG_SYS_CONSOLE_REBOOT_MAX_DELAY) {
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

static int cmd_version(int argc, char **argv)
{
    esp_chip_info_t chip_info;
    esp_chip_info(&chip_info);

    printf("Boorker v%s\n", BOORKER_VERSION_STRING);
    printf("ESP-IDF %s\n", esp_get_idf_version());
    printf("Chip: ESP32-S3 rev%d, %d cores\n", chip_info.revision, chip_info.cores);

    return 0;
}

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

static int cmd_uptime(int argc, char **argv)
{
    uint64_t uptime_us = esp_timer_get_time();
    uint64_t uptime_sec = uptime_us / 1000000;

    uint32_t hours = (uint32_t)(uptime_sec / 3600);
    uint32_t minutes = (uint32_t)((uptime_sec % 3600) / 60);
    uint32_t seconds = (uint32_t)(uptime_sec % 60);

    printf("Uptime: %luh %lum %lus\n",
           (unsigned long)hours, (unsigned long)minutes, (unsigned long)seconds);

    return 0;
}

static int cmd_status(int argc, char **argv)
{
    // Header with version and node name
    const credentials_t *id = credentials_get();
    printf("Boorker v%s", BOORKER_VERSION_STRING);
    if (id) {
        printf(" - %s", id->node_name);
    }
    printf("\n");

    // Uptime
    uint64_t uptime_us = esp_timer_get_time();
    uint64_t uptime_sec = uptime_us / 1000000;
    uint32_t hours = (uint32_t)(uptime_sec / 3600);
    uint32_t minutes = (uint32_t)((uptime_sec % 3600) / 60);
    uint32_t seconds = (uint32_t)(uptime_sec % 60);
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
    printf("WiFi: %s", wifi_mgr_get_state_name());
    if (wifi_mgr_get_ip(ip, sizeof(ip)) == ESP_OK && ip[0] != '\0') {
        printf(" (%s)", ip);
    }
    printf("\n");

#if CONFIG_TS_MGR_ENABLED
    // Tailscale status
    char ts_ip[16] = {0};
    printf("Tailscale: %s", ts_mgr_get_state_name());
    if (ts_mgr_get_ip(ts_ip, sizeof(ts_ip)) == ESP_OK && ts_ip[0] != '\0') {
        printf(" (%s)", ts_ip);
    }
    printf("\n");
#else
    printf("Tailscale: disabled\n");
#endif

    return 0;
}

static void print_motds(void)
{
    size_t count = 0;
    const motd_entry_t *motds = event_bus_get_motds(&count);
    if (motds == NULL || count == 0) {
        return;
    }

    printf("-------------------------------\n");
    for (size_t i = 0; i < count; i++) {
        const char *prefix;
        switch (motds[i].priority) {
            case MOTD_PRIORITY_WARNING:
                prefix = "[!]";
                break;
            case MOTD_PRIORITY_CRITICAL:
                prefix = "[!!!]";
                break;
            default:
                prefix = "[i]";
                break;
        }
        printf("%s %s\n", prefix, motds[i].message);
    }
    printf("-------------------------------\n");
}

esp_err_t sys_console_register(void)
{
    esp_err_t ret;
    esp_err_t first_error = ESP_OK;

    // Create mutex for thread-safe reboot state access
    s_reboot_mutex = xSemaphoreCreateMutex();
    if (s_reboot_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create reboot mutex");
        return ESP_ERR_NO_MEM;
    }

    // Reboot command - allocate argtable
    reboot_args.action = arg_str0(NULL, NULL, "[now|seconds|cancel]", "Reboot action");
    reboot_args.end = arg_end(1);

    if (reboot_args.action == NULL || reboot_args.end == NULL) {
        ESP_LOGE(TAG, "Failed to allocate argtable for reboot command");
        return ESP_ERR_NO_MEM;
    }

    const esp_console_cmd_t reboot_cmd = {
        .command = "reboot",
        .help = "Reboot device (default: 3s delay)",
        .hint = NULL,
        .func = &cmd_reboot,
        .argtable = &reboot_args,
    };
    ret = esp_console_cmd_register(&reboot_cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register 'reboot' command: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    // Version command
    const esp_console_cmd_t version_cmd = {
        .command = "version",
        .help = "Show firmware version",
        .hint = NULL,
        .func = &cmd_version,
    };
    ret = esp_console_cmd_register(&version_cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register 'version' command: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    // Free command
    const esp_console_cmd_t free_cmd = {
        .command = "free",
        .help = "Show memory statistics",
        .hint = NULL,
        .func = &cmd_free,
    };
    ret = esp_console_cmd_register(&free_cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register 'free' command: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    // Uptime command
    const esp_console_cmd_t uptime_cmd = {
        .command = "uptime",
        .help = "Show system uptime",
        .hint = NULL,
        .func = &cmd_uptime,
    };
    ret = esp_console_cmd_register(&uptime_cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register 'uptime' command: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    // Status command
    const esp_console_cmd_t status_cmd = {
        .command = "status",
        .help = "Show system status overview",
        .hint = NULL,
        .func = &cmd_status,
    };
    ret = esp_console_cmd_register(&status_cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register 'status' command: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    if (first_error != ESP_OK) {
        ESP_LOGW(TAG, "Some commands failed to register");
        return first_error;
    }

    ESP_LOGI(TAG, "Registered commands: reboot, version, free, uptime, status");

    print_motds();

    return ESP_OK;
}

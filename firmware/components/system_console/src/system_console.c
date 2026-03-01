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
static uint64_t s_reboot_scheduled_time = 0;  // Time when reboot was scheduled (us)
static uint64_t s_reboot_delay_us = 0;        // Total delay in microseconds

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

    s_reboot_delay_us = (uint64_t)delay_seconds * 1000000ULL;
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

    return (uint32_t)((s_reboot_delay_us - elapsed) / 1000000);
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

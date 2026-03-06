/**
 * @file sw420_console.c
 * @brief Console commands for SW-420 vibration sensor
 */

#include "sw420_driver.h"
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>

static const char *TAG = "sw420_console";
static sw420_handle_t s_handle = NULL;

static struct {
    struct arg_str *action;
    struct arg_str *param;
    struct arg_int *value;
    struct arg_end *end;
} vibration_args;

static void show_status(void)
{
    bool vibrating;
    uint32_t on_ms, off_ms;

    esp_err_t err = sw420_driver_read(s_handle, &vibrating);
    if (err != ESP_OK) {
        printf("Error reading sensor: %s\n", esp_err_to_name(err));
        return;
    }

    err = sw420_driver_get_config(s_handle, &on_ms, &off_ms);
    if (err != ESP_OK) {
        printf("Error reading config: %s\n", esp_err_to_name(err));
        return;
    }

    printf("VIBRATION SENSOR (sw420)\n");
    printf("----------------------------------------\n");
    printf("  State:    %s\n", vibrating ? "VIBRATING" : "IDLE");
    printf("\n");
    printf("  Config:\n");
    printf("    debounce_on_ms:  %lu\n", (unsigned long)on_ms);
    printf("    debounce_off_ms: %lu\n", (unsigned long)off_ms);
}

static void show_raw(int seconds)
{
    if (s_handle == NULL) {
        printf("Error: No sensor handle\n");
        return;
    }

    printf("Live GPIO sampling for %d seconds...\n\n", seconds);

    int samples = seconds * 2;  // 500ms intervals
    int high_count = 0;

    for (int i = 0; i < samples; i++) {
        bool raw = sw420_driver_read_raw(s_handle);
        if (raw) high_count++;

        printf("[%4.1fs] %s %s\n",
               i * 0.5f,
               raw ? "HIGH" : "LOW ",
               raw ? "████" : "____");

        vTaskDelay(pdMS_TO_TICKS(500));
    }

    int pct = (high_count * 100) / samples;
    printf("\nSummary: %d%% HIGH over %ds window\n", pct, seconds);

    if (pct == 0) {
        printf("Recommendation: No vibration detected. If pump is running,\n");
        printf("               turn pot counter-clockwise to increase sensitivity.\n");
    } else if (pct == 100) {
        printf("Recommendation: Constant HIGH. Turn pot clockwise to\n");
        printf("               decrease sensitivity.\n");
    } else if (pct < 30 || pct > 70) {
        printf("Recommendation: Signal looks intermittent. Try adjusting\n");
        printf("               pot or debounce settings.\n");
    } else {
        printf("Recommendation: Signal looks reasonable.\n");
    }
}

static int cmd_vibration(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&vibration_args);

    // No args - show status
    if (vibration_args.action->count == 0) {
        show_status();
        return 0;
    }

    if (nerrors != 0) {
        arg_print_errors(stderr, vibration_args.end, argv[0]);
        return 1;
    }

    const char *action = vibration_args.action->sval[0];

    // vibration status
    if (strcasecmp(action, "status") == 0) {
        show_status();
        return 0;
    }

    // vibration raw [seconds]
    if (strcasecmp(action, "raw") == 0) {
        int seconds = vibration_args.value->count > 0 ?
                      vibration_args.value->ival[0] : 10;
        if (seconds < 1) seconds = 1;
        if (seconds > 60) seconds = 60;
        show_raw(seconds);
        return 0;
    }

    // vibration config <param> <value>
    if (strcasecmp(action, "config") == 0) {
        if (vibration_args.param->count == 0) {
            // Show config
            uint32_t on_ms, off_ms;
            esp_err_t err = sw420_driver_get_config(s_handle, &on_ms, &off_ms);
            if (err != ESP_OK) {
                printf("Error reading config: %s\n", esp_err_to_name(err));
                return 1;
            }
            printf("debounce_on_ms:  %lu\n", (unsigned long)on_ms);
            printf("debounce_off_ms: %lu\n", (unsigned long)off_ms);
            return 0;
        }

        const char *param = vibration_args.param->sval[0];

        // vibration config save
        if (strcasecmp(param, "save") == 0) {
            esp_err_t err = sw420_driver_save_config(s_handle);
            if (err == ESP_OK) {
                printf("Config saved to NVS\n");
            } else {
                printf("Save failed: %s\n", esp_err_to_name(err));
                return 1;
            }
            return 0;
        }

        // vibration config <param> <value>
        if (vibration_args.value->count == 0) {
            printf("Usage: vibration config <param> <value>\n");
            printf("       vibration config save\n");
            return 1;
        }

        int value = vibration_args.value->ival[0];
        uint32_t on_ms, off_ms;
        esp_err_t err = sw420_driver_get_config(s_handle, &on_ms, &off_ms);
        if (err != ESP_OK) {
            printf("Error reading current config: %s\n", esp_err_to_name(err));
            return 1;
        }

        if (strcasecmp(param, "debounce_on_ms") == 0) {
            on_ms = value;
        } else if (strcasecmp(param, "debounce_off_ms") == 0) {
            off_ms = value;
        } else {
            printf("Unknown param '%s'\n", param);
            return 1;
        }

        err = sw420_driver_set_config(s_handle, on_ms, off_ms);
        if (err != ESP_OK) {
            printf("Error setting config: %s\n", esp_err_to_name(err));
            return 1;
        }
        printf("Set %s = %d\n", param, value);
        return 0;
    }

    printf("Unknown action '%s'\n", action);
    printf("Usage: vibration [status|raw [sec]|config [param] [value]]\n");
    return 1;
}

static void free_argtable(void)
{
    void *argtable[] = {vibration_args.action, vibration_args.param,
                        vibration_args.value, vibration_args.end};
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    vibration_args.action = NULL;
    vibration_args.param = NULL;
    vibration_args.value = NULL;
    vibration_args.end = NULL;
}

esp_err_t sw420_driver_register_console(sw420_handle_t handle)
{
    // Use provided handle or fall back to singleton
    if (handle != NULL) {
        s_handle = handle;
    } else {
        s_handle = sw420_driver_get_instance();
    }
    // Note: If s_handle is still NULL, commands will fail gracefully

    vibration_args.action = arg_str0(NULL, NULL, "<action>", "status|raw|config");
    vibration_args.param = arg_str0(NULL, NULL, "<param>", "Parameter name");
    vibration_args.value = arg_int0(NULL, NULL, "<value>", "Value");
    vibration_args.end = arg_end(3);

    if (!vibration_args.action || !vibration_args.param ||
        !vibration_args.value || !vibration_args.end) {
        ESP_LOGE(TAG, "Failed to allocate argtable");
        free_argtable();
        return ESP_ERR_NO_MEM;
    }

    const esp_console_cmd_t cmd = {
        .command = "vibration",
        .help = "Vibration sensor: vibration [status|raw [sec]|config [param] [value]]",
        .hint = NULL,
        .func = &cmd_vibration,
        .argtable = &vibration_args,
    };

    esp_err_t ret = esp_console_cmd_register(&cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register command: %s", esp_err_to_name(ret));
        free_argtable();
        return ret;
    }

    ESP_LOGI(TAG, "Registered command: vibration");
    return ESP_OK;
}

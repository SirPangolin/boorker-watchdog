/**
 * @file sensor_console.c
 * @brief Unified console interface for sensor operations and calibration
 *
 * All sensor interaction goes through the `sensor` command:
 *   sensor [status|read <id>|calibrate <id> [args...]]
 *
 * Calibration subcommands are driver-specific and dispatched by sensor ID.
 */

#include "sensor_manager.h"
#include "sw420_driver.h"
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>

static const char *TAG = "sensor_console";

static struct {
    struct arg_str *action;
    struct arg_str *id;
    struct arg_str *param1;
    struct arg_str *param2;
    struct arg_int *value;
    struct arg_end *end;
} sensor_args;

// --------------------------------------------------------------------------
// Show helpers
// --------------------------------------------------------------------------

static void show_all_sensors(void)
{
    size_t count = sensor_manager_get_sensor_count();
    printf("SENSORS (%zu configured)\n", count);
    printf("----------------------------------------\n");

    if (count == 0) {
        printf("  (none)\n");
        return;
    }

    for (size_t i = 0; i < count; i++) {
        sensor_reading_t reading;
        const char *id = sensor_manager_get_sensor_id(i);
        if (sensor_manager_get_reading_by_index(i, &reading) != ESP_OK) continue;

        printf("  %s\n", id ? id : "?");
        printf("    Status: %s\n", sensor_status_name(reading.status));
        if (reading.status == SENSOR_STATUS_ONLINE) {
            if (!isnan(reading.value)) {
                printf("    Value:  %.1f\n", reading.value);
            }
            if (!isnan(reading.value2)) {
                printf("    Value2: %.1f\n", reading.value2);
            }
        }
    }
}

static void show_sensor(const char *id)
{
    sensor_reading_t reading;
    esp_err_t err = sensor_manager_get_reading(id, &reading);
    if (err == ESP_ERR_NOT_FOUND) {
        printf("Sensor '%s' not found\n", id);
        return;
    }
    if (err != ESP_OK) {
        printf("Error reading sensor '%s': %s\n", id, esp_err_to_name(err));
        return;
    }

    printf("Sensor: %s\n", reading.sensor_id);
    printf("Status: %s\n", sensor_status_name(reading.status));
    if (!isnan(reading.value)) {
        printf("Value:  %.1f\n", reading.value);
    }
    if (!isnan(reading.value2)) {
        printf("Value2: %.1f\n", reading.value2);
    }
}

// --------------------------------------------------------------------------
// Calibration: sw420 vibration sensor
// --------------------------------------------------------------------------

#if CONFIG_SW420_DRIVER_ENABLED
static void calibrate_sw420_status(sw420_handle_t h)
{
    bool vibrating;
    uint32_t on_ms, off_ms;

    if (sw420_driver_read(h, &vibrating) != ESP_OK) {
        printf("Error reading sensor\n");
        return;
    }
    if (sw420_driver_get_config(h, &on_ms, &off_ms) != ESP_OK) {
        printf("Error reading config\n");
        return;
    }

    printf("VIBRATION CALIBRATION (sw420)\n");
    printf("----------------------------------------\n");
    printf("  State:           %s\n", vibrating ? "VIBRATING" : "IDLE");
    printf("  debounce_on_ms:  %lu\n", (unsigned long)on_ms);
    printf("  debounce_off_ms: %lu\n", (unsigned long)off_ms);
}

static void calibrate_sw420_raw(sw420_handle_t h, int seconds)
{
    if (seconds < 1) seconds = 1;
    if (seconds > 60) seconds = 60;

    printf("Live GPIO sampling for %d seconds...\n\n", seconds);

    int samples = seconds * 2;
    int high_count = 0;

    for (int i = 0; i < samples; i++) {
        bool raw = sw420_driver_read_raw(h);
        if (raw) high_count++;
        printf("[%4.1fs] %s %s\n", i * 0.5f, raw ? "HIGH" : "LOW ", raw ? "████" : "____");
        vTaskDelay(pdMS_TO_TICKS(500));
    }

    int pct = (high_count * 100) / samples;
    printf("\nSummary: %d%% HIGH over %ds window\n", pct, seconds);

    if (pct == 0) {
        printf("Tip: No vibration. Turn pot counter-clockwise to increase sensitivity.\n");
    } else if (pct == 100) {
        printf("Tip: Constant HIGH. Turn pot clockwise to decrease sensitivity.\n");
    } else if (pct < 30 || pct > 70) {
        printf("Tip: Intermittent signal. Adjust pot or debounce settings.\n");
    } else {
        printf("Tip: Signal looks reasonable.\n");
    }
}

static int calibrate_sw420(sw420_handle_t h, const char *subcmd, const char *param, int has_value, int value)
{
    if (!subcmd) {
        calibrate_sw420_status(h);
        return 0;
    }

    if (strcasecmp(subcmd, "raw") == 0) {
        calibrate_sw420_raw(h, has_value ? value : 10);
        return 0;
    }

    if (strcasecmp(subcmd, "config") == 0) {
        if (!param) {
            uint32_t on_ms, off_ms;
            if (sw420_driver_get_config(h, &on_ms, &off_ms) != ESP_OK) {
                printf("Error reading config\n");
                return 1;
            }
            printf("debounce_on_ms:  %lu\n", (unsigned long)on_ms);
            printf("debounce_off_ms: %lu\n", (unsigned long)off_ms);
            return 0;
        }

        if (strcasecmp(param, "save") == 0) {
            esp_err_t err = sw420_driver_save_config(h);
            printf("%s\n", err == ESP_OK ? "Config saved to NVS" : "Save failed");
            return err == ESP_OK ? 0 : 1;
        }

        if (!has_value) {
            printf("Usage: sensor calibrate vibration config <param> <value>\n");
            printf("       sensor calibrate vibration config save\n");
            return 1;
        }

        uint32_t on_ms, off_ms;
        if (sw420_driver_get_config(h, &on_ms, &off_ms) != ESP_OK) {
            printf("Error reading config\n");
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

        if (sw420_driver_set_config(h, on_ms, off_ms) != ESP_OK) {
            printf("Error setting config\n");
            return 1;
        }
        printf("Set %s = %d\n", param, value);
        return 0;
    }

    printf("Unknown subcommand '%s'\n", subcmd);
    printf("Usage: sensor calibrate vibration [raw [sec]|config [param] [value]]\n");
    return 1;
}
#endif

// --------------------------------------------------------------------------
// Main command handler
// --------------------------------------------------------------------------

static int cmd_sensor(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&sensor_args);

    if (sensor_args.action->count == 0) {
        show_all_sensors();
        return 0;
    }

    if (nerrors != 0) {
        arg_print_errors(stderr, sensor_args.end, argv[0]);
        return 1;
    }

    const char *action = sensor_args.action->sval[0];

    if (strcasecmp(action, "status") == 0) {
        show_all_sensors();
        return 0;
    }

    if (strcasecmp(action, "read") == 0) {
        const char *id = sensor_args.id->count > 0 ?
                         sensor_args.id->sval[0] : NULL;
        if (!id) {
            printf("Usage: sensor read <id>\n");
            return 1;
        }
        show_sensor(id);
        return 0;
    }

    if (strcasecmp(action, "calibrate") == 0) {
        const char *id = sensor_args.id->count > 0 ?
                         sensor_args.id->sval[0] : NULL;
        if (!id) {
            printf("Usage: sensor calibrate <id> [subcommand...]\n");
            printf("  Sensors with calibration:\n");
#if CONFIG_SW420_DRIVER_ENABLED
            printf("    vibration  — raw sampling, debounce config\n");
#endif
            return 1;
        }

#if CONFIG_SW420_DRIVER_ENABLED
        if (strcasecmp(id, "vibration") == 0) {
            sw420_handle_t h = sw420_driver_get_instance();
            if (!h) {
                printf("Vibration sensor not initialized\n");
                return 1;
            }
            const char *subcmd = sensor_args.param1->count > 0 ?
                                 sensor_args.param1->sval[0] : NULL;
            const char *param = sensor_args.param2->count > 0 ?
                                sensor_args.param2->sval[0] : NULL;
            int has_value = sensor_args.value->count > 0;
            int val = has_value ? sensor_args.value->ival[0] : 0;
            return calibrate_sw420(h, subcmd, param, has_value, val);
        }
#endif

        printf("No calibration available for sensor '%s'\n", id);
        return 1;
    }

    printf("Unknown action '%s'\n", action);
    printf("Usage: sensor [status|read <id>|calibrate <id> [args...]]\n");
    return 0;
}

// --------------------------------------------------------------------------
// Registration
// --------------------------------------------------------------------------

static void free_argtable(void)
{
    void *argtable[] = {sensor_args.action, sensor_args.id,
                        sensor_args.param1, sensor_args.param2,
                        sensor_args.value, sensor_args.end};
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    sensor_args.action = NULL;
    sensor_args.id = NULL;
    sensor_args.param1 = NULL;
    sensor_args.param2 = NULL;
    sensor_args.value = NULL;
    sensor_args.end = NULL;
}

esp_err_t sensor_manager_register_console(void)
{
    sensor_args.action = arg_str0(NULL, NULL, "<action>", "status|read|calibrate");
    sensor_args.id = arg_str0(NULL, NULL, "<id>", "Sensor ID");
    sensor_args.param1 = arg_str0(NULL, NULL, "<subcmd>", "Calibrate subcommand");
    sensor_args.param2 = arg_str0(NULL, NULL, "<param>", "Parameter name");
    sensor_args.value = arg_int0(NULL, NULL, "<value>", "Value");
    sensor_args.end = arg_end(5);

    if (!sensor_args.action || !sensor_args.id || !sensor_args.param1 ||
        !sensor_args.param2 || !sensor_args.value || !sensor_args.end) {
        ESP_LOGE(TAG, "Failed to allocate argtable");
        free_argtable();
        return ESP_ERR_NO_MEM;
    }

    const esp_console_cmd_t cmd = {
        .command = "sensor",
        .help = "Sensor commands: sensor [status|read <id>|calibrate <id> [args...]]",
        .hint = NULL,
        .func = &cmd_sensor,
        .argtable = &sensor_args,
    };

    esp_err_t ret = esp_console_cmd_register(&cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register command: %s", esp_err_to_name(ret));
        free_argtable();
        return ret;
    }

    ESP_LOGI(TAG, "Registered command: sensor");
    return ESP_OK;
}

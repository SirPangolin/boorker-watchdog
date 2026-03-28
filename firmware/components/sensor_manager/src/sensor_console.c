/**
 * @file sensor_console.c
 * @brief Console commands for sensor manager
 */

#include "sensor_manager.h"
#include "sw420_driver.h"
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <math.h>

static const char *TAG = "sensor_console";

static struct {
    struct arg_str *action;
    struct arg_str *sensor_id;
    struct arg_end *end;
} sensor_args;

static void show_all_sensors(void)
{
    size_t count = sensor_manager_get_sensor_count();
    printf("SENSORS (%zu configured)\n", count);
    printf("----------------------------------------\n");

    if (count == 0) {
        printf("  (none)\n");
        return;
    }

    // Show the default sensor
    sensor_reading_t reading;
    esp_err_t err = sensor_manager_get_reading("temp_humidity", &reading);

    if (err == ESP_OK) {
        printf("  %s\n", reading.sensor_id);
        printf("    Status: %s\n", sensor_status_name(reading.status));
        if (reading.status == SENSOR_STATUS_ONLINE) {
            if (!isnan(reading.value)) {
                printf("    Temp:   %.1f F\n", reading.value);
            }
            if (!isnan(reading.value2)) {
                printf("    Humid:  %.1f%%\n", reading.value2);
            }
        }
    } else if (err == ESP_ERR_NOT_FOUND) {
        printf("  temp_humidity: (not found)\n");
    } else {
        printf("  temp_humidity: (error: %s)\n", esp_err_to_name(err));
    }
}

static int cmd_sensor(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&sensor_args);

    // No args - show all sensors
    if (sensor_args.action->count == 0) {
        show_all_sensors();
        return 0;
    }

    if (nerrors != 0) {
        arg_print_errors(stderr, sensor_args.end, argv[0]);
        return 1;
    }

    const char *action = sensor_args.action->sval[0];

    // Handle: status
    if (strcasecmp(action, "status") == 0) {
        show_all_sensors();
        return 0;
    }

    // Handle: read <sensor_id>
    if (strcasecmp(action, "read") == 0) {
        const char *id = sensor_args.sensor_id->count > 0 ?
                         sensor_args.sensor_id->sval[0] : "temp_humidity";

        sensor_reading_t reading;
        esp_err_t err = sensor_manager_get_reading(id, &reading);
        if (err == ESP_ERR_NOT_FOUND) {
            printf("Sensor '%s' not found\n", id);
            return 1;
        }
        if (err != ESP_OK) {
            printf("Error reading sensor '%s': %s\n", id, esp_err_to_name(err));
            return 1;
        }

        printf("Sensor: %s\n", reading.sensor_id);
        printf("Status: %s\n", sensor_status_name(reading.status));
        if (!isnan(reading.value)) {
            printf("Temp:   %.1f F\n", reading.value);
        }
        if (!isnan(reading.value2)) {
            printf("Humid:  %.1f%%\n", reading.value2);
        }
        return 0;
    }

    printf("Unknown action '%s'\n", action);
    printf("Usage: sensor [status|read <id>]\n");
    return 1;
}

static void free_argtable(void)
{
    // Use arg_freetable to properly free argtable allocations
    void *argtable[] = {sensor_args.action, sensor_args.sensor_id, sensor_args.end};
    arg_freetable(argtable, sizeof(argtable) / sizeof(argtable[0]));
    sensor_args.action = NULL;
    sensor_args.sensor_id = NULL;
    sensor_args.end = NULL;
}

esp_err_t sensor_manager_register_console(void)
{
    sensor_args.action = arg_str0(NULL, NULL, "<action>", "status|read");
    sensor_args.sensor_id = arg_str0(NULL, NULL, "<id>", "Sensor ID");
    sensor_args.end = arg_end(2);

    if (sensor_args.action == NULL || sensor_args.sensor_id == NULL ||
        sensor_args.end == NULL) {
        ESP_LOGE(TAG, "Failed to allocate argtable");
        free_argtable();
        return ESP_ERR_NO_MEM;
    }

    const esp_console_cmd_t cmd = {
        .command = "sensor",
        .help = "Sensor status: sensor [status|read <id>]",
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

#if CONFIG_SW420_DRIVER_ENABLED
    // Register sw420 vibration console commands (owned by sensor_manager, not main)
    sw420_handle_t sw420 = sw420_driver_get_instance();
    if (sw420) {
        ret = sw420_driver_register_console(sw420);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "SW420 console init failed: %s", esp_err_to_name(ret));
        }
    }
#endif

    return ESP_OK;
}

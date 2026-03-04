/**
 * @file led_console.c
 * @brief Console commands for status LED control
 */

#include "status_led.h"
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "led_console";

/**
 * @brief Command handler: led
 * Show status LED status
 */
static int cmd_led_status(int argc, char **argv)
{
    printf("Status LED:\n");
    printf("  Enabled: %s\n", status_led_is_enabled() ? "yes" : "no");
    printf("  Brightness: %d%%\n", status_led_get_brightness());
    printf("  Mode: %s\n", status_led_is_alerts_only() ? "alerts only" : "all states");
    printf("  Current state: %s\n", status_led_state_name(status_led_get_state()));
    return 0;
}

/**
 * @brief Command handler: led_on
 * Enable status LED and save to NVS
 */
static int cmd_led_on(int argc, char **argv)
{
    esp_err_t ret = status_led_set_enabled(true);
    if (ret != ESP_OK) {
        printf("Failed to enable LED: %s\n", esp_err_to_name(ret));
        return 1;
    }

    ret = status_led_save_config();
    if (ret != ESP_OK) {
        printf("Warning: Failed to save config: %s\n", esp_err_to_name(ret));
    }

    printf("Status LED enabled.\n");
    return 0;
}

/**
 * @brief Command handler: led_off
 * Disable status LED and save to NVS
 */
static int cmd_led_off(int argc, char **argv)
{
    esp_err_t ret = status_led_set_enabled(false);
    if (ret != ESP_OK) {
        printf("Failed to disable LED: %s\n", esp_err_to_name(ret));
        return 1;
    }

    ret = status_led_save_config();
    if (ret != ESP_OK) {
        printf("Warning: Failed to save config: %s\n", esp_err_to_name(ret));
    }

    printf("Status LED disabled.\n");
    return 0;
}

// Brightness command arguments
static struct {
    struct arg_int *percent;
    struct arg_end *end;
} brightness_args;

/**
 * @brief Command handler: led_brightness <percent>
 * Set brightness 0-100 and save to NVS
 */
static int cmd_led_brightness(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&brightness_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, brightness_args.end, argv[0]);
        return 1;
    }

    if (brightness_args.percent->count == 0) {
        // No argument - show current brightness
        printf("Brightness: %d%%\n", status_led_get_brightness());
        return 0;
    }

    int percent = brightness_args.percent->ival[0];
    if (percent < 0 || percent > 100) {
        printf("Error: Brightness must be 0-100.\n");
        return 1;
    }

    esp_err_t ret = status_led_set_brightness((uint8_t)percent);
    if (ret != ESP_OK) {
        printf("Failed to set brightness: %s\n", esp_err_to_name(ret));
        return 1;
    }

    ret = status_led_save_config();
    if (ret != ESP_OK) {
        printf("Warning: Failed to save config: %s\n", esp_err_to_name(ret));
    }

    printf("Brightness set to %d%%.\n", percent);
    return 0;
}

/**
 * @brief Command handler: led_alerts
 * Set alerts-only mode and save to NVS
 */
static int cmd_led_alerts(int argc, char **argv)
{
    esp_err_t ret = status_led_set_alerts_only(true);
    if (ret != ESP_OK) {
        printf("Failed to set alerts-only mode: %s\n", esp_err_to_name(ret));
        return 1;
    }

    ret = status_led_save_config();
    if (ret != ESP_OK) {
        printf("Warning: Failed to save config: %s\n", esp_err_to_name(ret));
    }

    printf("LED mode set to alerts only.\n");
    return 0;
}

/**
 * @brief Command handler: led_all
 * Set all-states mode and save to NVS
 */
static int cmd_led_all(int argc, char **argv)
{
    esp_err_t ret = status_led_set_alerts_only(false);
    if (ret != ESP_OK) {
        printf("Failed to set all-states mode: %s\n", esp_err_to_name(ret));
        return 1;
    }

    ret = status_led_save_config();
    if (ret != ESP_OK) {
        printf("Warning: Failed to save config: %s\n", esp_err_to_name(ret));
    }

    printf("LED mode set to all states.\n");
    return 0;
}

esp_err_t status_led_register_console(void)
{
    esp_err_t ret;
    esp_err_t first_error = ESP_OK;

    // led - status command
    const esp_console_cmd_t led_cmd = {
        .command = "led",
        .help = "Show status LED status",
        .hint = NULL,
        .func = &cmd_led_status,
    };
    ret = esp_console_cmd_register(&led_cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register 'led' command: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    // led_on - enable command
    const esp_console_cmd_t led_on_cmd = {
        .command = "led_on",
        .help = "Enable status LED",
        .hint = NULL,
        .func = &cmd_led_on,
    };
    ret = esp_console_cmd_register(&led_on_cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register 'led_on' command: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    // led_off - disable command
    const esp_console_cmd_t led_off_cmd = {
        .command = "led_off",
        .help = "Disable status LED",
        .hint = NULL,
        .func = &cmd_led_off,
    };
    ret = esp_console_cmd_register(&led_off_cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register 'led_off' command: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    // led_brightness - brightness command with argtable
    brightness_args.percent = arg_int0(NULL, NULL, "<percent>", "Brightness 0-100");
    brightness_args.end = arg_end(1);

    if (brightness_args.percent == NULL || brightness_args.end == NULL) {
        ESP_LOGE(TAG, "Failed to allocate argtable for led_brightness command");
        arg_freetable((void **)&brightness_args, sizeof(brightness_args) / sizeof(void *));
        return ESP_ERR_NO_MEM;
    }

    const esp_console_cmd_t led_brightness_cmd = {
        .command = "led_brightness",
        .help = "Set LED brightness (0-100)",
        .hint = NULL,
        .func = &cmd_led_brightness,
        .argtable = &brightness_args,
    };
    ret = esp_console_cmd_register(&led_brightness_cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register 'led_brightness' command: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    // led_alerts - alerts-only mode command
    const esp_console_cmd_t led_alerts_cmd = {
        .command = "led_alerts",
        .help = "Set LED to alerts-only mode",
        .hint = NULL,
        .func = &cmd_led_alerts,
    };
    ret = esp_console_cmd_register(&led_alerts_cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register 'led_alerts' command: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    // led_all - all-states mode command
    const esp_console_cmd_t led_all_cmd = {
        .command = "led_all",
        .help = "Set LED to show all states",
        .hint = NULL,
        .func = &cmd_led_all,
    };
    ret = esp_console_cmd_register(&led_all_cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register 'led_all' command: %s", esp_err_to_name(ret));
        if (first_error == ESP_OK) first_error = ret;
    }

    if (first_error != ESP_OK) {
        ESP_LOGW(TAG, "Some LED commands failed to register");
        return first_error;
    }

    ESP_LOGI(TAG, "Registered commands: led, led_on, led_off, led_brightness, led_alerts, led_all");
    return ESP_OK;
}

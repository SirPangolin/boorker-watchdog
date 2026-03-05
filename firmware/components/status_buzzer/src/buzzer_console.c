/**
 * @file buzzer_console.c
 * @brief Console commands for status buzzer
 *
 * Implements CLI commands for testing and configuring the status buzzer:
 * - buzzer              Show status and all preset volumes
 * - buzzer on/off       Enable/disable master
 * - buzzer alerts_only on/off  Filter mode
 * - buzzer volume <preset> <0-100>  Set preset volume
 * - buzzer test <preset>  Play preset once
 * - buzzer stop         Stop current sound
 */

#include "status_buzzer.h"
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include <stdio.h>
#include <string.h>
#include <strings.h>  // For strcasecmp

static const char *TAG = "buzzer_console";

static struct {
    struct arg_str *action;
    struct arg_str *param1;
    struct arg_int *param2;
    struct arg_end *end;
} buzzer_args;

/**
 * @brief Parse preset name string to enum (case-insensitive)
 * @param name Preset name string
 * @param preset Output preset value
 * @return true if valid preset name, false otherwise
 */
static bool parse_preset_name(const char *name, buzzer_preset_t *preset)
{
    if (strcasecmp(name, "chirp") == 0) {
        *preset = BUZZER_PRESET_CHIRP;
        return true;
    }
    if (strcasecmp(name, "double_beep") == 0) {
        *preset = BUZZER_PRESET_DOUBLE_BEEP;
        return true;
    }
    if (strcasecmp(name, "triple_beep") == 0) {
        *preset = BUZZER_PRESET_TRIPLE_BEEP;
        return true;
    }
    if (strcasecmp(name, "alarm") == 0) {
        *preset = BUZZER_PRESET_ALARM;
        return true;
    }
    if (strcasecmp(name, "solid") == 0) {
        *preset = BUZZER_PRESET_SOLID;
        return true;
    }
    return false;
}

/**
 * @brief Show buzzer status and all preset volumes
 */
static void show_buzzer_status(void)
{
    printf("Buzzer: %s\n", status_buzzer_is_enabled() ? "ON" : "OFF");
    printf("Alerts only: %s\n", status_buzzer_is_alerts_only() ? "YES" : "NO");
    printf("Volumes:\n");
    printf("  CHIRP:       %3d%%\n", status_buzzer_get_preset_volume(BUZZER_PRESET_CHIRP));
    printf("  DOUBLE_BEEP: %3d%%\n", status_buzzer_get_preset_volume(BUZZER_PRESET_DOUBLE_BEEP));
    printf("  TRIPLE_BEEP: %3d%%\n", status_buzzer_get_preset_volume(BUZZER_PRESET_TRIPLE_BEEP));
    printf("  ALARM:       %3d%%\n", status_buzzer_get_preset_volume(BUZZER_PRESET_ALARM));
    printf("  SOLID:       %3d%%\n", status_buzzer_get_preset_volume(BUZZER_PRESET_SOLID));
}

/**
 * @brief Command handler: buzzer [action] [param1] [param2]
 */
static int cmd_buzzer(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&buzzer_args);

    // No args - show status
    if (buzzer_args.action->count == 0) {
        show_buzzer_status();
        return 0;
    }

    if (nerrors != 0) {
        arg_print_errors(stderr, buzzer_args.end, argv[0]);
        return 1;
    }

    const char *action = buzzer_args.action->sval[0];
    esp_err_t ret;

    // Handle: on
    if (strcasecmp(action, "on") == 0) {
        ret = status_buzzer_set_enabled(true);
        if (ret != ESP_OK) {
            printf("Failed to enable buzzer: %s\n", esp_err_to_name(ret));
            return 1;
        }
        ret = status_buzzer_save_config();
        if (ret != ESP_OK) {
            printf("Warning: Failed to save config: %s\n", esp_err_to_name(ret));
        }
        printf("Buzzer enabled.\n");
        return 0;
    }

    // Handle: off
    if (strcasecmp(action, "off") == 0) {
        ret = status_buzzer_set_enabled(false);
        if (ret != ESP_OK) {
            printf("Failed to disable buzzer: %s\n", esp_err_to_name(ret));
            return 1;
        }
        ret = status_buzzer_save_config();
        if (ret != ESP_OK) {
            printf("Warning: Failed to save config: %s\n", esp_err_to_name(ret));
        }
        printf("Buzzer disabled.\n");
        return 0;
    }

    // Handle: alerts_only on/off
    if (strcasecmp(action, "alerts_only") == 0) {
        if (buzzer_args.param1->count == 0) {
            printf("Alerts only: %s\n", status_buzzer_is_alerts_only() ? "YES" : "NO");
            printf("Usage: buzzer alerts_only on|off\n");
            return 0;
        }
        const char *param = buzzer_args.param1->sval[0];
        bool alerts_only;
        if (strcasecmp(param, "on") == 0) {
            alerts_only = true;
        } else if (strcasecmp(param, "off") == 0) {
            alerts_only = false;
        } else {
            printf("Error: Expected 'on' or 'off', got '%s'\n", param);
            return 1;
        }
        ret = status_buzzer_set_alerts_only(alerts_only);
        if (ret != ESP_OK) {
            printf("Failed to set alerts_only: %s\n", esp_err_to_name(ret));
            return 1;
        }
        ret = status_buzzer_save_config();
        if (ret != ESP_OK) {
            printf("Warning: Failed to save config: %s\n", esp_err_to_name(ret));
        }
        printf("Alerts only: %s\n", alerts_only ? "ON" : "OFF");
        return 0;
    }

    // Handle: volume <preset> <0-100>
    if (strcasecmp(action, "volume") == 0) {
        if (buzzer_args.param1->count == 0 || buzzer_args.param2->count == 0) {
            printf("Usage: buzzer volume <preset> <0-100>\n");
            printf("Presets: chirp, double_beep, triple_beep, alarm, solid\n");
            return 1;
        }
        const char *preset_name = buzzer_args.param1->sval[0];
        int volume = buzzer_args.param2->ival[0];

        buzzer_preset_t preset;
        if (!parse_preset_name(preset_name, &preset)) {
            printf("Error: Unknown preset '%s'\n", preset_name);
            printf("Valid presets: chirp, double_beep, triple_beep, alarm, solid\n");
            return 1;
        }

        if (volume < 0 || volume > 100) {
            printf("Error: Volume must be 0-100, got %d\n", volume);
            return 1;
        }

        ret = status_buzzer_set_preset_volume(preset, (uint8_t)volume);
        if (ret != ESP_OK) {
            printf("Failed to set volume: %s\n", esp_err_to_name(ret));
            return 1;
        }
        ret = status_buzzer_save_config();
        if (ret != ESP_OK) {
            printf("Warning: Failed to save config: %s\n", esp_err_to_name(ret));
        }
        printf("Volume for %s set to %d%%.\n", buzzer_preset_name(preset), volume);
        return 0;
    }

    // Handle: test <preset>
    if (strcasecmp(action, "test") == 0) {
        if (buzzer_args.param1->count == 0) {
            printf("Usage: buzzer test <preset>\n");
            printf("Presets: chirp, double_beep, triple_beep, alarm, solid\n");
            return 1;
        }
        const char *preset_name = buzzer_args.param1->sval[0];
        buzzer_preset_t preset;
        if (!parse_preset_name(preset_name, &preset)) {
            printf("Error: Unknown preset '%s'\n", preset_name);
            printf("Valid presets: chirp, double_beep, triple_beep, alarm, solid\n");
            return 1;
        }
        ret = status_buzzer_play(preset);
        if (ret != ESP_OK) {
            printf("Failed to play preset: %s\n", esp_err_to_name(ret));
            return 1;
        }
        printf("Playing %s...\n", buzzer_preset_name(preset));
        return 0;
    }

    // Handle: stop
    if (strcasecmp(action, "stop") == 0) {
        ret = status_buzzer_stop();
        if (ret != ESP_OK) {
            printf("Failed to stop buzzer: %s\n", esp_err_to_name(ret));
            return 1;
        }
        printf("Buzzer stopped.\n");
        return 0;
    }

    // Unknown action
    printf("Error: Unknown action '%s'\n", action);
    printf("Usage: buzzer [on|off|alerts_only|volume|test|stop]\n");
    return 1;
}

esp_err_t status_buzzer_register_console(void)
{
    // Create argtable
    buzzer_args.action = arg_str0(NULL, NULL, "<action>", "on|off|alerts_only|volume|test|stop");
    buzzer_args.param1 = arg_str0(NULL, NULL, "<param>", "Preset name or on/off");
    buzzer_args.param2 = arg_int0(NULL, NULL, "<value>", "Volume 0-100");
    buzzer_args.end = arg_end(2);

    // Check allocations
    if (!buzzer_args.action || !buzzer_args.param1 || !buzzer_args.param2 || !buzzer_args.end) {
        ESP_LOGE(TAG, "Failed to allocate argtable");
        // Free any that succeeded
        if (buzzer_args.action) free(buzzer_args.action);
        if (buzzer_args.param1) free(buzzer_args.param1);
        if (buzzer_args.param2) free(buzzer_args.param2);
        if (buzzer_args.end) free(buzzer_args.end);
        return ESP_ERR_NO_MEM;
    }

    // Register command
    const esp_console_cmd_t cmd = {
        .command = "buzzer",
        .help = "Buzzer control: buzzer [on|off|alerts_only|volume|test|stop]",
        .hint = NULL,
        .func = &cmd_buzzer,
        .argtable = &buzzer_args,
    };

    esp_err_t ret = esp_console_cmd_register(&cmd);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register 'buzzer' command: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "Registered command: buzzer");
    return ESP_OK;
}

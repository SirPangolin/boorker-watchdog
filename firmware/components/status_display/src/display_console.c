/**
 * @file display_console.c
 * @brief Console commands for status display
 */

#include "sdkconfig.h"

#if CONFIG_STATUS_DISPLAY_ENABLED

#include "status_display.h"
#include "esp_console.h"
#include "esp_log.h"

static int cmd_disp(int argc, char **argv)
{
    printf("Display: enabled\n");
    printf("Use PRG button for navigation\n");
    printf("  Short press: next screen\n");
    printf("  Long press (3s): display off\n");
    printf("  Very long (10s): reboot\n");
    return 0;
}

esp_err_t status_display_register_console(void)
{
    const esp_console_cmd_t cmd = {
        .command = "disp",
        .help = "Show display status and controls",
        .hint = NULL,
        .func = &cmd_disp,
    };

    esp_err_t ret = esp_console_cmd_register(&cmd);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI("disp_console", "Registered command: disp");
    return ESP_OK;
}

#endif /* CONFIG_STATUS_DISPLAY_ENABLED */

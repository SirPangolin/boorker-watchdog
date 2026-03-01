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

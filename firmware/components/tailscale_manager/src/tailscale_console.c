#include "tailscale_console.h"
#include "esp_log.h"

static const char *TAG = "ts_console";

esp_err_t ts_console_register(void)
{
    ESP_LOGI(TAG, "Console commands registered (stub)");
    return ESP_OK;
}

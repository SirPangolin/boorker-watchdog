#include "wifi_power_internal.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_wifi.h"

static const char *TAG = "wifi_power";

esp_err_t wifi_power_enable(void)
{
#if CONFIG_WIFI_MGR_ENABLE_POWER_SAVE
    ESP_LOGI(TAG, "Enabling WiFi power save (modem sleep)...");

    esp_err_t ret = esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to enable power save: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "WiFi power save enabled");
    return ESP_OK;
#else
    ESP_LOGI(TAG, "WiFi power save disabled in config");
    return ESP_OK;
#endif
}

esp_err_t wifi_power_disable(void)
{
    ESP_LOGI(TAG, "Disabling WiFi power save...");

    esp_err_t ret = esp_wifi_set_ps(WIFI_PS_NONE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to disable power save: %s", esp_err_to_name(ret));
        return ret;
    }

    ESP_LOGI(TAG, "WiFi power save disabled");
    return ESP_OK;
}

#include "wifi_mdns_internal.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include "mdns.h"

static const char *TAG = "wifi_mdns";

static bool s_mdns_started = false;

esp_err_t wifi_mdns_start(const char *hostname)
{
#if CONFIG_WIFI_MGR_ENABLE_MDNS
    if (s_mdns_started) {
        ESP_LOGW(TAG, "mDNS already started");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Starting mDNS as '%s.local'...", hostname);

    esp_err_t ret = mdns_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init mDNS: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = mdns_hostname_set(hostname);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set hostname: %s", esp_err_to_name(ret));
        mdns_free();
        return ret;
    }

    ret = mdns_instance_name_set("Boorker IoT Device");
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set instance name: %s", esp_err_to_name(ret));
        // Not critical, continue
    }

    // Add HTTP service for discovery
    ret = mdns_service_add(NULL, "_http", "_tcp", 80, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to add HTTP service: %s", esp_err_to_name(ret));
        // Not critical, continue
    }

    s_mdns_started = true;
    ESP_LOGI(TAG, "mDNS started: %s.local", hostname);

    return ESP_OK;
#else
    ESP_LOGI(TAG, "mDNS disabled in config");
    return ESP_OK;
#endif
}

void wifi_mdns_stop(void)
{
#if CONFIG_WIFI_MGR_ENABLE_MDNS
    if (s_mdns_started) {
        mdns_free();
        s_mdns_started = false;
        ESP_LOGI(TAG, "mDNS stopped");
    }
#endif
}

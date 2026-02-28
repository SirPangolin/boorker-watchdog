#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

#include "wifi_manager.h"

static const char *TAG = "boorker";

static void wifi_event_callback(wifi_mgr_event_t event, void *ctx)
{
    switch (event) {
        case WIFI_MGR_EVENT_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected - ready for services");
            break;

        case WIFI_MGR_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi disconnected - services paused");
            break;

        case WIFI_MGR_EVENT_PROVISIONING:
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "WiFi Provisioning Mode");
            ESP_LOGI(TAG, "1. Install 'ESP BLE Prov' app (iOS/Android)");
            ESP_LOGI(TAG, "2. Scan for BLE device");
            ESP_LOGI(TAG, "3. Enter PIN when prompted");
            ESP_LOGI(TAG, "4. Select WiFi network and enter password");
            ESP_LOGI(TAG, "========================================");
            break;

        case WIFI_MGR_EVENT_PROVISIONED:
            ESP_LOGI(TAG, "Credentials saved - connecting...");
            break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Boorker starting...");

    // Initialize NVS (required for WiFi credential storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "NVS initialized");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Free PSRAM: %lu bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // Initialize WiFi manager
    wifi_mgr_config_t wifi_config = {
        .device_name = "boorker-dev",
        .start_provisioning = false,  // Only if no stored credentials
        .callback = wifi_event_callback,
        .callback_ctx = NULL,
    };

    ret = wifi_mgr_init(&wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi manager init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    EventGroupHandle_t events = wifi_mgr_get_event_group();
    xEventGroupWaitBits(events, WIFI_MGR_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    // Get IP address
    char ip[16];
    if (wifi_mgr_get_ip(ip, sizeof(ip)) == ESP_OK) {
        ESP_LOGI(TAG, "Connected with IP: %s", ip);
    }

    // Main loop - heartbeat
    while (1) {
        ESP_LOGI(TAG, "Heartbeat - state: %s, heap: %lu",
                 wifi_mgr_get_state_name(),
                 esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

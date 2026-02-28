#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "nvs_flash.h"

static const char *TAG = "boorker-watchdog";

void app_main(void)
{
    ESP_LOGI(TAG, "Boorker starting...");

    // Initialize NVS (required for WiFi and storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "NVS initialized");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Free PSRAM: %lu bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    while (1) {
        ESP_LOGI(TAG, "Heartbeat - heap: %lu", esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
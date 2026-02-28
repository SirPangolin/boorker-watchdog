#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_console.h"
#include "nvs_flash.h"

#include "wifi_manager.h"
#include "tailscale_manager.h"

static const char *TAG = "boorker";

// Flag to signal main task to init Tailscale (can't init from event callback - stack overflow)
static volatile bool s_wifi_connected = false;
static volatile bool s_tailscale_initialized = false;

static void tailscale_callback(ts_mgr_event_t event, void *ctx)
{
    char ip[16];
    switch (event) {
        case TS_MGR_EVENT_CONNECTED:
            if (ts_mgr_get_ip(ip, sizeof(ip)) == ESP_OK) {
                ESP_LOGI(TAG, "Tailscale connected: %s", ip);
            }
            break;

        case TS_MGR_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "Tailscale disconnected - reconnecting...");
            break;

        case TS_MGR_EVENT_UNCONFIGURED:
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "Tailscale not configured");
            ESP_LOGI(TAG, "Use 'ts_auth <key>' to set auth key");
            ESP_LOGI(TAG, "Get key from: https://login.tailscale.com/admin/settings/keys");
            ESP_LOGI(TAG, "========================================");
            break;

        case TS_MGR_EVENT_AUTH_FAILED:
            ESP_LOGW(TAG, "Tailscale auth failed - key may be expired");
            break;

        case TS_MGR_EVENT_RECONNECT_EXHAUSTED:
            ESP_LOGW(TAG, "Tailscale reconnection exhausted");
            break;

        case TS_MGR_EVENT_KEY_UPDATED:
            ESP_LOGI(TAG, "Tailscale auth key updated");
            break;
    }
}

static void wifi_event_callback(wifi_mgr_event_t event, void *ctx)
{
    switch (event) {
        case WIFI_MGR_EVENT_CONNECTED:
            // Don't init Tailscale here - callback runs on sys_evt task with limited stack
            // Set flag for main task to handle
            s_wifi_connected = true;
            ESP_LOGI(TAG, "WiFi connected - Tailscale will init from main task");
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

        case WIFI_MGR_EVENT_RECONNECT_EXHAUSTED:
            ESP_LOGW(TAG, "WiFi reconnection attempts exhausted");
            break;
    }
}

static void init_console(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "boorker>";
    repl_config.max_cmdline_length = 256;

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

    // Register Tailscale console commands
    ts_console_register();

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    ESP_LOGI(TAG, "Console ready. Type 'help' for commands.");
}

void app_main(void)
{
    ESP_LOGI(TAG, "Boorker starting...");

    // Initialize NVS (required for WiFi and Tailscale credential storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "NVS initialized");
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Free PSRAM: %lu bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // Initialize console for ts_auth command
    init_console();

    // Initialize WiFi manager (Tailscale init happens in callback)
    wifi_mgr_config_t wifi_config = {
        .device_name = "boorker-dev",
        .start_provisioning = false,
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

    // Get WiFi IP address
    char ip[16];
    if (wifi_mgr_get_ip(ip, sizeof(ip)) == ESP_OK) {
        ESP_LOGI(TAG, "WiFi connected with IP: %s", ip);
    }

    // Initialize Tailscale from main task (has adequate stack - can't init from callback)
    ESP_LOGI(TAG, "Initializing Tailscale from main task...");
    ts_mgr_config_t ts_config = {
        .device_name = "boorker-dev",
        .callback = tailscale_callback,
        .callback_ctx = NULL,
    };
    ts_mgr_init(&ts_config);

    // Main loop - heartbeat
    while (1) {
        ESP_LOGI(TAG, "Heartbeat - WiFi: %s, Tailscale: %s, heap: %lu",
                 wifi_mgr_get_state_name(),
                 ts_mgr_get_state_name(),
                 esp_get_free_heap_size());
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

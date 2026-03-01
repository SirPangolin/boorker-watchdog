#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_console.h"
#include "nvs_flash.h"

#include "version.h"
#include "wifi_manager.h"
#if CONFIG_TS_MGR_ENABLED
#include "tailscale_manager.h"
#endif
#include "device_identity.h"
#include "web_auth.h"
#include "web_server.h"
#include "system_console.h"
#include "led_feedback.h"

static const char *TAG = "boorker";

#if CONFIG_TS_MGR_ENABLED
static void tailscale_callback(ts_mgr_event_t event, void *ctx)
{
    char ip[16];
    switch (event) {
        case TS_MGR_EVENT_CONNECTED:
            led_feedback_clear_state(LED_FB_TAILSCALE_CONNECTING);
            if (ts_mgr_get_ip(ip, sizeof(ip)) == ESP_OK) {
                ESP_LOGI(TAG, "Tailscale connected: %s", ip);
            } else {
                ESP_LOGW(TAG, "Tailscale connected but IP retrieval failed");
            }
            break;

        case TS_MGR_EVENT_DISCONNECTED:
            led_feedback_set_state(LED_FB_TAILSCALE_CONNECTING);
            ESP_LOGW(TAG, "Tailscale disconnected - reconnecting...");
            break;

        case TS_MGR_EVENT_UNCONFIGURED:
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "Tailscale not configured");
            ESP_LOGI(TAG, "Use 'ts_auth <key>' to set auth key");
            ESP_LOGI(TAG, "Get key from: https://login.tailscale.com/admin/settings/keys");
            ESP_LOGI(TAG, "========================================");
            break;

        case TS_MGR_EVENT_RECONNECT_EXHAUSTED:
            ESP_LOGW(TAG, "Tailscale reconnection exhausted");
            break;

        case TS_MGR_EVENT_KEY_UPDATED:
            led_feedback_set_state(LED_FB_TAILSCALE_CONNECTING);
            ESP_LOGI(TAG, "Tailscale auth key updated");
            break;
    }
}
#endif

static void wifi_event_callback(wifi_mgr_event_t event, void *ctx)
{
    switch (event) {
        case WIFI_MGR_EVENT_CONNECTED:
            led_feedback_clear_state(LED_FB_WIFI_CONNECTING);
            led_feedback_clear_state(LED_FB_WIFI_RECONNECTING);
            led_feedback_clear_state(LED_FB_WIFI_PROVISIONING);
            led_feedback_set_state(LED_FB_CONNECTED);
            // Tailscale init happens in main task after xEventGroupWaitBits returns
            // Don't init here - callback runs on sys_evt task with limited stack
            ESP_LOGI(TAG, "WiFi connected");
            break;

        case WIFI_MGR_EVENT_DISCONNECTED:
            led_feedback_clear_state(LED_FB_CONNECTED);
            led_feedback_set_state(LED_FB_WIFI_RECONNECTING);
            ESP_LOGW(TAG, "WiFi disconnected - services paused");
            break;

        case WIFI_MGR_EVENT_PROVISIONING:
            led_feedback_set_state(LED_FB_WIFI_PROVISIONING);
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "WiFi Provisioning Mode");
            ESP_LOGI(TAG, "1. Install 'ESP BLE Prov' app (iOS/Android)");
            ESP_LOGI(TAG, "2. Scan for BLE device");
            ESP_LOGI(TAG, "3. Enter PIN when prompted");
            ESP_LOGI(TAG, "4. Select WiFi network and enter password");
            ESP_LOGI(TAG, "========================================");
            break;

        case WIFI_MGR_EVENT_PROVISIONED:
            led_feedback_clear_state(LED_FB_WIFI_PROVISIONING);
            led_feedback_set_state(LED_FB_WIFI_CONNECTING);
            ESP_LOGI(TAG, "Credentials saved - connecting...");
            break;

        case WIFI_MGR_EVENT_RECONNECT_EXHAUSTED:
            ESP_LOGW(TAG, "WiFi reconnection attempts exhausted");
            break;

        case WIFI_MGR_EVENT_COUNT:
            // Sentinel value for bounds checking - should never occur
            break;
    }
}

static void init_console(void)
{
    esp_err_t ret;
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "boorker>";
    repl_config.max_cmdline_length = 256;

    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();

    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));

#if CONFIG_TS_MGR_ENABLED
    // Register Tailscale console commands
    ret = ts_console_register();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Tailscale console init failed: %s (commands may be unavailable)",
                 esp_err_to_name(ret));
        // Non-fatal - continue without tailscale commands
    }
#endif

    // Register system console commands
    ret = system_console_register();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "System console init failed: %s (commands may be unavailable)",
                 esp_err_to_name(ret));
        // Non-fatal - continue without system commands
    }

    // Register LED feedback console commands
    ret = led_feedback_register_console();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LED console init failed: %s", esp_err_to_name(ret));
    }

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    ESP_LOGI(TAG, "Console ready. Type 'help' for commands.");
}

void app_main(void)
{
#if CONFIG_TS_MGR_ENABLED
    // Reduce log spam from chatty components
    esp_log_level_set("microlink", ESP_LOG_WARN);
    esp_log_level_set("microlink_disco", ESP_LOG_WARN);
    esp_log_level_set("microlink_coord", ESP_LOG_WARN);
#endif

    ESP_LOGI(TAG, "Boorker v%s starting...", BOORKER_VERSION_STRING);

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

    // Initialize LED feedback early (shows boot state)
    ret = led_feedback_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LED feedback init failed: %s (continuing without LED)",
                 esp_err_to_name(ret));
        // Non-fatal - continue without LED feedback
    }

    // Initialize device identity (generates credentials on first boot)
    ret = device_identity_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Device identity init failed: %s", esp_err_to_name(ret));
        return;
    }

    const device_identity_t *identity = device_identity_get();
    if (identity == NULL) {
        ESP_LOGE(TAG, "Failed to get device identity");
        return;
    }
    ESP_LOGI(TAG, "Device: %s", identity->node_name);

    // Show credentials on first boot (until OLED is implemented)
    if (device_identity_is_first_boot()) {
        led_feedback_set_state(LED_FB_FIRST_BOOT);
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "FIRST BOOT - SAVE THESE CREDENTIALS:");
        ESP_LOGI(TAG, "  Web Password: %s", identity->web_password);
        ESP_LOGI(TAG, "  AP Password: %s", identity->ap_password);
        ESP_LOGI(TAG, "  BLE PoP: %s", identity->ble_pop);
        ESP_LOGI(TAG, "========================================");
    }

    // Initialize console for ts_auth command
    init_console();

    // Initialize WiFi manager (Tailscale init happens in callback)
    wifi_mgr_config_t wifi_config = {
        .device_name = identity->node_name,  // Use generated name instead of hardcoded
        .start_provisioning = false,
        .callback = wifi_event_callback,
        .callback_ctx = NULL,
    };

    ret = wifi_mgr_init(&wifi_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi manager init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Show connecting state while waiting for WiFi
    led_feedback_set_state(LED_FB_WIFI_CONNECTING);

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

    // Initialize web auth (required for secure web server operation)
    ret = web_auth_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Web auth init failed: %s - web server disabled for security",
                 esp_err_to_name(ret));
        // Don't start web server without authentication - security risk
    } else {
        // Start web server only if auth is available
        ret = web_server_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Web server start failed: %s", esp_err_to_name(ret));
            // Continue without web server
        } else {
            if (ip[0] != '\0') {
                ESP_LOGI(TAG, "Web server running at http://%s/", ip);
            } else {
                ESP_LOGI(TAG, "Web server started");
            }
        }
    }

#if CONFIG_TS_MGR_ENABLED
    // Initialize Tailscale from main task (has adequate stack - can't init from callback)
    ESP_LOGI(TAG, "Initializing Tailscale from main task...");
    ts_mgr_config_t ts_config = {
        .device_name = identity->node_name,  // Use generated name
        .callback = tailscale_callback,
        .callback_ctx = NULL,
    };
    ret = ts_mgr_init(&ts_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Tailscale manager init failed: %s", esp_err_to_name(ret));
        // Continue without Tailscale - WiFi still works
    }
#else
    ESP_LOGI(TAG, "Tailscale disabled in config");
#endif

    // Main loop - heartbeat
    while (1) {
#if CONFIG_TS_MGR_ENABLED
        ESP_LOGI(TAG, "Heartbeat - WiFi: %s, Tailscale: %s, heap: %lu",
                 wifi_mgr_get_state_name(),
                 ts_mgr_get_state_name(),
                 esp_get_free_heap_size());
#else
        ESP_LOGI(TAG, "Heartbeat - WiFi: %s, heap: %lu",
                 wifi_mgr_get_state_name(),
                 esp_get_free_heap_size());
#endif
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

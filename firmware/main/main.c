#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_heap_caps.h"
#include "esp_console.h"
#include "nvs_flash.h"
#include "esp_partition.h"

#include "version.h"
#include "wifi_manager.h"
#if CONFIG_TS_MGR_ENABLED
#include "tailscale_manager.h"
#endif
#include "credentials.h"
#include "system_state.h"
#include "event_bus.h"
#include "web_auth.h"
#include "http_server.h"
#include "sys_console.h"
#include "status_led.h"
#include "status_buzzer.h"
#include "sensor_manager.h"
#include "ota_manager.h"
#if CONFIG_SW420_DRIVER_ENABLED
#include "sw420_driver.h"
#endif

static const char *TAG = "boorker";

#if CONFIG_TS_MGR_ENABLED
static void tailscale_callback(ts_mgr_event_t event, void *ctx)
{
    char ip[16] = {0};
    switch (event) {
        case TS_MGR_EVENT_CONNECTED:
            event_bus_clear_state(EVENT_TAILSCALE_CONNECTING);
            if (ts_mgr_get_ip(ip, sizeof(ip)) == ESP_OK) {
                ESP_LOGI(TAG, "Tailscale connected: %s", ip);
            } else {
                ESP_LOGW(TAG, "Tailscale connected but IP retrieval failed");
            }
            break;

        case TS_MGR_EVENT_DISCONNECTED:
            event_bus_set_state(EVENT_TAILSCALE_CONNECTING);
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
            event_bus_set_state(EVENT_TAILSCALE_CONNECTING);
            ESP_LOGI(TAG, "Tailscale auth key updated");
            break;
    }
}
#endif

static void wifi_event_callback(wifi_mgr_event_t event, void *ctx)
{
    switch (event) {
        case WIFI_MGR_EVENT_CONNECTED:
            // Only clear WiFi-related states - wifi_manager doesn't own FIRST_BOOT
            // FIRST_BOOT is cleared by web_auth when password is changed
            event_bus_clear_state(EVENT_WIFI_CONNECTING);
            event_bus_clear_state(EVENT_WIFI_RECONNECTING);
            event_bus_clear_state(EVENT_WIFI_PROVISIONING);
            event_bus_set_state(EVENT_CONNECTED);
            // Tailscale init happens in main task after xEventGroupWaitBits returns
            // Don't init here - callback runs on sys_evt task with limited stack
            ESP_LOGI(TAG, "WiFi connected");
            break;

        case WIFI_MGR_EVENT_DISCONNECTED:
            event_bus_clear_state(EVENT_CONNECTED);
            event_bus_set_state(EVENT_WIFI_RECONNECTING);
            ESP_LOGW(TAG, "WiFi disconnected - services paused");
            break;

        case WIFI_MGR_EVENT_PROVISIONING:
            event_bus_set_state(EVENT_WIFI_PROVISIONING);
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "WiFi Provisioning Mode");
            ESP_LOGI(TAG, "1. Install 'ESP BLE Prov' app (iOS/Android)");
            ESP_LOGI(TAG, "2. Scan for BLE device");
            ESP_LOGI(TAG, "3. Enter PIN when prompted");
            ESP_LOGI(TAG, "4. Select WiFi network and enter password");
            ESP_LOGI(TAG, "========================================");
            break;

        case WIFI_MGR_EVENT_PROVISIONED:
            // Just clear provisioning state - WIFI_CONNECTING was set at startup
            // and will be cleared by CONNECTED handler when connection succeeds
            event_bus_clear_state(EVENT_WIFI_PROVISIONING);
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

static void on_sensor_reading(const sensor_reading_t *reading, void *ctx)
{
    (void)ctx;

    // Digital sensors (vibration, float_switch) use value=0/1, value2=duration_ms
    if (strstr(reading->sensor_id, "vibration") != NULL ||
        strstr(reading->sensor_id, "float") != NULL) {
        ESP_LOGI(TAG, "Sensor '%s': %s, duration %.0fms [%s]",
                 reading->sensor_id,
                 reading->value > 0.5f ? "ACTIVE" : "IDLE",
                 reading->value2,
                 sensor_status_name(reading->status));
    } else {
        // Analog sensors (temp/humidity)
        ESP_LOGI(TAG, "Sensor '%s': %.1f F, %.1f%% humidity [%s]",
                 reading->sensor_id,
                 reading->value,
                 reading->value2,
                 sensor_status_name(reading->status));
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
    ret = sys_console_register();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "System console init failed: %s (commands may be unavailable)",
                 esp_err_to_name(ret));
        // Non-fatal - continue without system commands
    }

    // Register status LED console commands
    ret = status_led_register_console();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LED console init failed: %s", esp_err_to_name(ret));
    }

    // Register status buzzer console commands
    ret = status_buzzer_register_console();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Buzzer console init failed: %s", esp_err_to_name(ret));
    }

    // Register sensor manager console commands
    ret = sensor_manager_register_console();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Sensor console init failed: %s", esp_err_to_name(ret));
    }

#if CONFIG_SW420_DRIVER_ENABLED
    // Register SW420 vibration sensor console commands
    ret = sw420_driver_register_console(NULL);  // Uses singleton
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "SW420 console init failed: %s", esp_err_to_name(ret));
    }
#endif

    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    ESP_LOGI(TAG, "Console ready. Type 'help' for commands.");
}

// Wipe NVS + nvs_keys and generate fresh encryption key.
// Used for corruption recovery — any NVS/key issue triggers a full reset.
static esp_err_t nvs_full_wipe_and_reinit(const esp_partition_t *keys_part)
{
    ESP_LOGW(TAG, "Performing full NVS wipe (data + encryption key)");

    esp_err_t ret = nvs_flash_erase();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS partition erase failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ret = esp_partition_erase_range(keys_part, 0, keys_part->size);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "nvs_keys partition erase failed: %s", esp_err_to_name(ret));
        return ret;
    }

    nvs_sec_cfg_t sec_cfg;
    ret = nvs_flash_generate_keys(keys_part, &sec_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS key generation failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_flash_secure_init(&sec_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "NVS secure init failed after full wipe: %s", esp_err_to_name(ret));
    }
    return ret;
}

static esp_err_t init_nvs_encrypted(void)
{
    const esp_partition_t *keys_part = esp_partition_find_first(
        ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_DATA_NVS_KEYS, "nvs_keys");
    if (keys_part == NULL) {
        ESP_LOGE(TAG, "nvs_keys partition not found — check partition table!");
        ESP_LOGW(TAG, "Falling back to UNENCRYPTED NVS — credentials stored in plaintext");
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
            ESP_LOGW(TAG, "NVS needs erase (%s)", esp_err_to_name(ret));
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        return ret;
    }

    nvs_sec_cfg_t sec_cfg;
    esp_err_t ret = nvs_flash_read_security_cfg(keys_part, &sec_cfg);

    if (ret == ESP_ERR_NVS_KEYS_NOT_INITIALIZED) {
        // First boot or post-factory-reset: generate key, init NVS
        ESP_LOGI(TAG, "Generating NVS encryption keys (first boot)");
        ret = nvs_flash_generate_keys(keys_part, &sec_cfg);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "NVS key generation failed: %s", esp_err_to_name(ret));
            return ret;
        }
        ret = nvs_flash_secure_init(&sec_cfg);
        if (ret != ESP_OK) {
            // Stale NVS data from pre-encryption or prior key — wipe everything
            ESP_LOGW(TAG, "NVS init failed after key gen (%s) — wiping", esp_err_to_name(ret));
            ret = nvs_full_wipe_and_reinit(keys_part);
            if (ret == ESP_OK) ESP_LOGI(TAG, "NVS initialized (encrypted, after wipe)");
            return ret;
        }
        ESP_LOGI(TAG, "NVS initialized (encrypted, first boot)");
        return ESP_OK;
    }

    if (ret != ESP_OK) {
        // Key partition corrupt — full wipe
        ESP_LOGE(TAG, "NVS key read failed (%s) — corruption, wiping everything", esp_err_to_name(ret));
        ret = nvs_full_wipe_and_reinit(keys_part);
        if (ret == ESP_OK) ESP_LOGI(TAG, "NVS initialized (encrypted, after wipe)");
        return ret;
    }

    // Key loaded OK — try init
    ret = nvs_flash_secure_init(&sec_cfg);
    if (ret != ESP_OK) {
        // NVS data corrupt — full wipe
        ESP_LOGE(TAG, "NVS secure init failed (%s) — corruption, wiping everything", esp_err_to_name(ret));
        ret = nvs_full_wipe_and_reinit(keys_part);
        if (ret == ESP_OK) ESP_LOGI(TAG, "NVS initialized (encrypted, after wipe)");
        return ret;
    }

    ESP_LOGI(TAG, "NVS initialized (encrypted)");
    return ESP_OK;
}

void app_main(void)
{
#if CONFIG_TS_MGR_ENABLED
    // Reduce log spam from chatty components
    esp_log_level_set("microlink", ESP_LOG_WARN);
    esp_log_level_set("microlink_disco", ESP_LOG_WARN);
    esp_log_level_set("microlink_coord", ESP_LOG_WARN);
#endif

    // Boot banner (ASCII-only for serial compatibility)
    printf("\n");
    printf("  +====================================================================+\n");
    printf("  |                                                                    |\n");
    printf("  |   ____   ___   ___  ____  _  _______ ____                          |\n");
    printf("  |  | __ ) / _ \\ / _ \\|  _ \\| |/ / ____|  _ \\                         |\n");
    printf("  |  |  _ \\| | | | | | | |_) | ' /|  _| | |_) |                        |\n");
    printf("  |  | |_) | |_| | |_| |  _ <| . \\| |___|  _ <                         |\n");
    printf("  |  |____/ \\___/ \\___/|_| \\_\\_|\\_\\_____|_| \\_\\   WATCHDOG            |\n");
    printf("  |                                                                    |\n");
    printf("  |  IoT Sensor Mesh Platform                              v%-8s  |\n", BOORKER_VERSION_STRING);
    printf("  |  %-64s  |\n", BOORKER_GITHUB_URL);
    printf("  |                                                                    |\n");
    printf("  +====================================================================+\n");
    printf("\n");

    // Initialize NVS with encryption (requires nvs_keys partition)
    ESP_ERROR_CHECK(init_nvs_encrypted());
    ESP_LOGI(TAG, "Free heap: %lu bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Free PSRAM: %lu bytes", heap_caps_get_free_size(MALLOC_CAP_SPIRAM));

    // Initialize device state early (others depend on claimed status)
    esp_err_t ret = system_state_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "System state init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Mark current firmware as valid to prevent rollback on next boot
    ret = ota_manager_mark_valid();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "OTA mark valid failed: %s (continuing without rollback protection)",
                 esp_err_to_name(ret));
    }

    // Initialize credentials (generates secrets on first boot)
    ret = credentials_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Credentials init failed: %s", esp_err_to_name(ret));
        return;
    }

    const credentials_t *identity = credentials_get();
    if (identity == NULL) {
        ESP_LOGE(TAG, "Failed to get credentials");
        return;
    }
    ESP_LOGI(TAG, "Device: %s", identity->node_name);

    // Initialize event bus before publishers
    ret = event_bus_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Event bus init failed: %s", esp_err_to_name(ret));
        return;
    }

    // Initialize status LED (registers with event bus)
    ret = status_led_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Status LED init failed: %s (continuing without LED)",
                 esp_err_to_name(ret));
        // Non-fatal - continue without status LED
    }

    // Initialize status buzzer (registers with event bus)
    ret = status_buzzer_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Buzzer init failed: %s", esp_err_to_name(ret));
        // Continue - buzzer is not critical
    }

    // Initialize sensor manager
    ret = sensor_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Sensor manager init failed: %s", esp_err_to_name(ret));
        // Continue without sensors - not critical for basic operation
    } else {
        sensor_manager_register_callback(on_sensor_reading, NULL);
        ret = sensor_manager_start();
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Sensor polling start failed: %s", esp_err_to_name(ret));
        }
    }

    // Show credentials on first boot (until OLED is implemented)
    if (!system_state_is_claimed()) {
        event_bus_set_state(EVENT_FIRST_BOOT);
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
    event_bus_set_state(EVENT_WIFI_CONNECTING);

    // Wait for WiFi connection
    ESP_LOGI(TAG, "Waiting for WiFi connection...");
    EventGroupHandle_t events = wifi_mgr_get_event_group();
    xEventGroupWaitBits(events, WIFI_MGR_CONNECTED_BIT,
                        pdFALSE, pdTRUE, portMAX_DELAY);

    // Get WiFi IP address
    char ip[16] = {0};
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
        ret = http_server_start();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Web server start failed: %s", esp_err_to_name(ret));
            // Continue without web server
        } else {
            if (ip[0] != '\0') {
#if CONFIG_HTTP_SERVER_HTTPS_ENABLED
                ESP_LOGI(TAG, "Web server running at https://%s/", ip);
#else
                ESP_LOGI(TAG, "Web server running at http://%s/", ip);
#endif
            } else {
                ESP_LOGI(TAG, "Web server started");
            }

            // Initialize OTA manager (needs network and HTTP server)
            ret = ota_manager_init();
            if (ret != ESP_OK) {
                ESP_LOGW(TAG, "OTA manager init failed: %s (continuing without OTA updates)",
                         esp_err_to_name(ret));
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

#include "wifi_prov_internal.h"
#include "sdkconfig.h"

#include "esp_log.h"
#include "esp_wifi.h"
#include "wifi_provisioning/manager.h"
#include "wifi_provisioning/scheme_ble.h"

static const char *TAG = "wifi_prov";

static bool s_prov_active = false;
static esp_event_handler_instance_t s_prov_event_instance = NULL;

// Provisioning event handler
static void prov_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    switch (event_id) {
        case WIFI_PROV_START:
            ESP_LOGI(TAG, "Provisioning started");
            break;

        case WIFI_PROV_CRED_RECV: {
            wifi_sta_config_t *sta = (wifi_sta_config_t *)event_data;
            ESP_LOGI(TAG, "Received credentials for SSID: %s", sta->ssid);
            break;
        }

        case WIFI_PROV_CRED_FAIL: {
            wifi_prov_sta_fail_reason_t *reason = (wifi_prov_sta_fail_reason_t *)event_data;
            ESP_LOGE(TAG, "Provisioning failed: %s",
                     (*reason == WIFI_PROV_STA_AUTH_ERROR) ?
                     "Auth failed" : "AP not found");
            break;
        }

        case WIFI_PROV_CRED_SUCCESS:
            ESP_LOGI(TAG, "Provisioning successful");
            break;

        case WIFI_PROV_END:
            ESP_LOGI(TAG, "Provisioning ended");
            wifi_prov_mgr_deinit();
            s_prov_active = false;
            break;

        default:
            break;
    }
}

esp_err_t wifi_prov_start(const char *device_name)
{
    ESP_LOGI(TAG, "Starting BLE provisioning as '%s'...", device_name);

    // Initialize provisioning manager
    wifi_prov_mgr_config_t config = {
        .scheme = wifi_prov_scheme_ble,
        .scheme_event_handler = WIFI_PROV_SCHEME_BLE_EVENT_HANDLER_FREE_BTDM,
    };

    esp_err_t ret = wifi_prov_mgr_init(config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init provisioning manager: %s", esp_err_to_name(ret));
        return ret;
    }

    // Register provisioning event handler
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_PROV_EVENT, ESP_EVENT_ANY_ID, &prov_event_handler, NULL,
        &s_prov_event_instance));

    // Check if already provisioned
    bool provisioned = false;
    wifi_prov_mgr_is_provisioned(&provisioned);
    if (provisioned) {
        ESP_LOGI(TAG, "Already provisioned, clearing for re-provisioning");
        // Continue anyway to allow re-provisioning
    }

    // Configure security (with Proof-of-Possession)
    wifi_prov_security_t security = WIFI_PROV_SECURITY_1;
    const char *pop = CONFIG_WIFI_MGR_PROV_POP;

    // Configure service name (BLE device name)
    wifi_prov_scheme_ble_set_service_uuid((uint8_t *)"\x12\x34\x56\x78\x12\x34\x56\x78\x12\x34\x56\x78\x12\x34\x56\x78");

    // Start provisioning
    ret = wifi_prov_mgr_start_provisioning(security, pop, device_name, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start provisioning: %s", esp_err_to_name(ret));
        wifi_prov_mgr_deinit();
        return ret;
    }

    s_prov_active = true;
    ESP_LOGI(TAG, "BLE provisioning active. Use ESP BLE Prov app to configure.");
    ESP_LOGI(TAG, "Proof-of-Possession PIN: %s", pop);

    return ESP_OK;
}

void wifi_prov_stop(void)
{
    if (s_prov_active) {
        wifi_prov_mgr_stop_provisioning();
        if (s_prov_event_instance) {
            esp_event_handler_instance_unregister(WIFI_PROV_EVENT, ESP_EVENT_ANY_ID,
                s_prov_event_instance);
            s_prov_event_instance = NULL;
        }
        wifi_prov_mgr_deinit();
        s_prov_active = false;
    }
}

bool wifi_prov_is_active(void)
{
    return s_prov_active;
}

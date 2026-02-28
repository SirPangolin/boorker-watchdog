#include "wifi_manager.h"
#include "esp_log.h"

static const char *TAG = "wifi_manager";

esp_err_t wifi_mgr_init(const wifi_mgr_config_t *config)
{
    ESP_LOGI(TAG, "wifi_mgr_init stub called");
    return ESP_OK;
}

bool wifi_mgr_is_connected(void)
{
    return false;
}

esp_err_t wifi_mgr_get_ip(char *buf, size_t len)
{
    return ESP_ERR_INVALID_STATE;
}

const char* wifi_mgr_get_state_name(void)
{
    return "STUB";
}

EventGroupHandle_t wifi_mgr_get_event_group(void)
{
    return NULL;
}

esp_err_t wifi_mgr_start_provisioning(void)
{
    return ESP_OK;
}

esp_err_t wifi_mgr_clear_credentials(void)
{
    return ESP_OK;
}

esp_err_t wifi_mgr_stop(void)
{
    return ESP_OK;
}

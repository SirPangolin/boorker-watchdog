#include "tailscale_manager.h"
#include "esp_log.h"

static const char *TAG = "tailscale_mgr";

esp_err_t ts_mgr_init(const ts_mgr_config_t *config)
{
    ESP_LOGI(TAG, "Tailscale manager init (stub)");
    return ESP_OK;
}

esp_err_t ts_mgr_stop(void)
{
    return ESP_OK;
}

bool ts_mgr_is_connected(void)
{
    return false;
}

bool ts_mgr_is_configured(void)
{
    return false;
}

const char* ts_mgr_get_state_name(void)
{
    return "STUB";
}

esp_err_t ts_mgr_get_ip(char *buf, size_t len)
{
    return ESP_ERR_INVALID_STATE;
}

esp_err_t ts_mgr_set_auth_key(const char *key)
{
    return ESP_OK;
}

esp_err_t ts_mgr_clear_auth_key(void)
{
    return ESP_OK;
}

bool ts_mgr_has_auth_key(void)
{
    return false;
}

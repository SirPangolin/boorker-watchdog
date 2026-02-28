#include "tailscale_nvs.h"

esp_err_t ts_nvs_store_key(const char *key)
{
    return ESP_OK;
}

esp_err_t ts_nvs_load_key(char *buf, size_t buf_len)
{
    return ESP_ERR_NOT_FOUND;
}

esp_err_t ts_nvs_clear_key(void)
{
    return ESP_OK;
}

bool ts_nvs_has_key(void)
{
    return false;
}

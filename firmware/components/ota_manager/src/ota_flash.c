#include "ota_manager_internal.h"
#include "esp_log.h"

static const char *TAG = "ota_flash";

esp_err_t ota_flash_begin(uint32_t image_size)
{
    (void)image_size;
    ESP_LOGW(TAG, "Not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ota_flash_write(const void *data, size_t len)
{
    (void)data;
    (void)len;
    ESP_LOGW(TAG, "Not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ota_flash_verify_hash(const char *expected_sha256)
{
    (void)expected_sha256;
    ESP_LOGW(TAG, "Not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ota_flash_end(void)
{
    ESP_LOGW(TAG, "Not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t ota_flash_abort(void)
{
    ESP_LOGW(TAG, "Not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

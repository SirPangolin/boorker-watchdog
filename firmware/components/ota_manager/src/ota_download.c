#include "ota_manager_internal.h"
#include "esp_log.h"

static const char *TAG = "ota_download";

esp_err_t ota_download_start(const ota_update_info_t *info, ota_progress_cb_t cb, void *ctx)
{
    (void)info;
    (void)cb;
    (void)ctx;
    ESP_LOGW(TAG, "Not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

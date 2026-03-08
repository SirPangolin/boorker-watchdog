#include "ota_manager_internal.h"
#include "esp_log.h"

static const char *TAG = "ota_github";

esp_err_t ota_github_check_releases(void)
{
    ESP_LOGW(TAG, "Not yet implemented");
    return ESP_ERR_NOT_SUPPORTED;
}

bool ota_github_version_newer(const char *current, const char *candidate)
{
    (void)current;
    (void)candidate;
    return false;
}

#include "tailscale_nvs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "ts_nvs";

#define NVS_NAMESPACE "tailscale"
#define NVS_KEY_AUTH  "auth_key"

esp_err_t ts_nvs_store_key(const char *key)
{
    if (key == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = nvs_set_str(handle, NVS_KEY_AUTH, key);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set auth key in NVS: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    nvs_close(handle);
    // Never log the key itself - only length for debugging
    ESP_LOGI(TAG, "Auth key stored (length: %d)", strlen(key));
    return ESP_OK;
}

esp_err_t ts_nvs_load_key(char *buf, size_t buf_len)
{
    if (buf == NULL || buf_len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        // NVS namespace doesn't exist yet - not an error
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            return ESP_ERR_NOT_FOUND;
        }
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t len = buf_len;
    ret = nvs_get_str(handle, NVS_KEY_AUTH, buf, &len);
    nvs_close(handle);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Auth key loaded (length: %d)", strlen(buf));
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGE(TAG, "Failed to load auth key: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t ts_nvs_clear_key(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            return ESP_OK;  // Nothing to clear
        }
        return ret;
    }

    ret = nvs_erase_key(handle, NVS_KEY_AUTH);
    if (ret == ESP_OK || ret == ESP_ERR_NVS_NOT_FOUND) {
        esp_err_t commit_ret = nvs_commit(handle);
        if (commit_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit NVS clear: %s", esp_err_to_name(commit_ret));
            nvs_close(handle);
            return commit_ret;
        }
        ret = ESP_OK;
    }

    nvs_close(handle);
    ESP_LOGI(TAG, "Auth key cleared");
    return ret;
}

bool ts_nvs_has_key(void)
{
    nvs_handle_t handle;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        return false;
    }

    size_t len = 0;
    ret = nvs_get_str(handle, NVS_KEY_AUTH, NULL, &len);
    nvs_close(handle);

    return (ret == ESP_OK && len > 0);
}

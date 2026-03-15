#include "ota_manager_internal.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "version.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <inttypes.h>
#include <string.h>

static const char *TAG = "ota_download";

esp_err_t ota_download_start(const ota_update_info_t *info, ota_progress_cb_t cb, void *ctx)
{
    if (info == NULL || info->download_url[0] == '\0') {
        ESP_LOGE(TAG, "Invalid update info");
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_FAIL;
    esp_http_client_handle_t client = NULL;

    const size_t buf_size = CONFIG_OTA_MANAGER_HTTP_BUF_SIZE_KB * 1024;
    char *buf = heap_caps_malloc(buf_size, MALLOC_CAP_SPIRAM);
    if (!buf) {
        ESP_LOGE(TAG, "Failed to allocate download buffer");
        /* No flash session exists yet — nothing to abort */
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "Starting OTA download from %s (%"PRIu32" bytes)",
             info->download_url, info->size_bytes);

    /* Step 1: Prepare the OTA partition */
    ret = ota_flash_begin(info->size_bytes);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ota_flash_begin failed: %s", esp_err_to_name(ret));
        heap_caps_free(buf);
        return ret;
    }

    /* Step 2: Set up HTTPS client */
    esp_http_client_config_t config = {
        .url = info->download_url,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .timeout_ms = 15000,
        .max_redirection_count = 5,
    };

    client = esp_http_client_init(&config);
    if (client == NULL) {
        ESP_LOGE(TAG, "Failed to init HTTP client");
        ret = ESP_FAIL;
        goto cleanup;
    }

    esp_http_client_set_header(client, "User-Agent", "boorker-watchdog/" BOORKER_VERSION_STRING);

    ret = esp_http_client_open(client, 0); /* 0 = no POST data */
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    int content_length = esp_http_client_fetch_headers(client);
    int status_code = esp_http_client_get_status_code(client);
    ESP_LOGI(TAG, "HTTP status %d, content-length %d", status_code, content_length);

    if (status_code < 200 || status_code >= 300) {
        ESP_LOGE(TAG, "HTTP error: status %d", status_code);
        ret = ESP_FAIL;
        goto cleanup;
    }

    /* Warn if server-reported size doesn't match release metadata */
    if (content_length > 0 && info->size_bytes > 0 &&
        (uint32_t)content_length != info->size_bytes) {
        ESP_LOGW(TAG, "Content-Length %d differs from expected %" PRIu32 " bytes",
                 content_length, info->size_bytes);
    }

    /* Step 3: Read and flash in chunks (5 minute total timeout) */
    const TickType_t download_start = xTaskGetTickCount();
    const TickType_t max_download_ticks = pdMS_TO_TICKS(5 * 60 * 1000);
    int bytes_read;
    while ((bytes_read = esp_http_client_read(client, buf, buf_size)) > 0) {
        if ((xTaskGetTickCount() - download_start) > max_download_ticks) {
            ESP_LOGE(TAG, "Download timeout exceeded (5 minutes)");
            ret = ESP_ERR_TIMEOUT;
            goto cleanup;
        }
        ret = ota_flash_write(buf, bytes_read);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "ota_flash_write failed: %s", esp_err_to_name(ret));
            goto cleanup;
        }

        if (cb != NULL) {
            cb(g_ota.session.bytes_written, g_ota.session.total_bytes, ctx);
        }
    }

    if (bytes_read < 0) {
        ESP_LOGE(TAG, "HTTP read error");
        ret = ESP_FAIL;
        goto cleanup;
    }

    /* Step 4: Verify hash if provided */
    if (info->has_sha256) {
        ret = ota_flash_verify_hash(info->sha256);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Hash verification failed: %s", esp_err_to_name(ret));
            goto cleanup;
        }
        ESP_LOGI(TAG, "SHA-256 hash verified");
    } else {
        ESP_LOGW(TAG, "No SHA-256 hash available — firmware integrity NOT verified");
    }

    /* Finalize: validate image and set boot partition */
    ret = ota_flash_end();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ota_flash_end failed: %s", esp_err_to_name(ret));
        goto cleanup;
    }

    ESP_LOGI(TAG, "OTA download complete");

cleanup:
    heap_caps_free(buf);
    if (client) {
        esp_http_client_close(client);
        esp_http_client_cleanup(client);
    }
    /* This function owns the flash session it created —
     * abort on failure so the caller doesn't need to */
    if (ret != ESP_OK) {
        ota_flash_abort();
    }
    return ret;
}

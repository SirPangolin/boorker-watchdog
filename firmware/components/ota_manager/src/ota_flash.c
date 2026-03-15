#include "ota_manager_internal.h"
#include "esp_ota_ops.h"
#include "esp_log.h"
#include "mbedtls/sha256.h"
#include <inttypes.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

static const char *TAG = "ota_flash";

static mbedtls_sha256_context s_sha256_ctx;
static bool s_sha256_active = false;

esp_err_t ota_flash_begin(uint32_t image_size)
{
    const esp_partition_t *partition = esp_ota_get_next_update_partition(NULL);
    if (partition == NULL) {
        ESP_LOGE(TAG, "No OTA partition found");
        return ESP_ERR_NOT_FOUND;
    }

    if (image_size > partition->size) {
        ESP_LOGE(TAG, "Image size %" PRIu32 " exceeds partition size %" PRIu32,
                 image_size, partition->size);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_ota_handle_t handle = 0;
    esp_err_t err = esp_ota_begin(partition, image_size, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
        return err;
    }

    g_ota.session.update_partition = partition;
    g_ota.session.ota_handle = handle;
    g_ota.session.bytes_written = 0;
    g_ota.session.total_bytes = image_size;

    /* Initialize SHA256 streaming context */
    mbedtls_sha256_init(&s_sha256_ctx);
    int ret = mbedtls_sha256_starts(&s_sha256_ctx, 0); /* 0 = SHA-256 */
    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_sha256_starts failed: %d", ret);
        esp_ota_abort(handle);
        g_ota.session.ota_handle = 0;
        g_ota.session.update_partition = NULL;
        mbedtls_sha256_free(&s_sha256_ctx);
        return ESP_FAIL;
    }
    s_sha256_active = true;

    ESP_LOGI(TAG, "OTA flash begin: partition=%s, size=%" PRIu32,
             partition->label, image_size);
    return ESP_OK;
}

esp_err_t ota_flash_write(const void *data, size_t len)
{
    if (g_ota.session.ota_handle == 0) {
        ESP_LOGE(TAG, "Write called with no active flash session");
        return ESP_ERR_INVALID_STATE;
    }

    if (g_ota.session.total_bytes > 0 &&
        g_ota.session.bytes_written + len > g_ota.session.total_bytes) {
        ESP_LOGE(TAG, "Write would exceed declared size (written=%"PRIu32" + len=%zu > total=%"PRIu32")",
                 g_ota.session.bytes_written, len, g_ota.session.total_bytes);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t err = esp_ota_write(g_ota.session.ota_handle, data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
        return err;
    }

    /* Update SHA256 before bumping bytes_written so state stays consistent
     * if the hash update fails */
    if (s_sha256_active) {
        int ret = mbedtls_sha256_update(&s_sha256_ctx, (const unsigned char *)data, len);
        if (ret != 0) {
            ESP_LOGE(TAG, "mbedtls_sha256_update failed: %d (data already written to flash, "
                     "hash verification will be unreliable)", ret);
            mbedtls_sha256_free(&s_sha256_ctx);
            s_sha256_active = false;
            return ESP_FAIL;
        }
    }

    g_ota.session.bytes_written += len;

    return ESP_OK;
}

esp_err_t ota_flash_verify_hash(const char *expected_sha256)
{
    if (expected_sha256 == NULL) {
        ESP_LOGE(TAG, "Expected SHA256 is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    if (strlen(expected_sha256) != 64) {
        ESP_LOGE(TAG, "Expected SHA256 has invalid length: %zu", strlen(expected_sha256));
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_sha256_active) {
        ESP_LOGE(TAG, "SHA256 context not active");
        return ESP_FAIL;
    }

    /* Finalize the SHA256 digest */
    uint8_t sha256_bin[32];
    int ret = mbedtls_sha256_finish(&s_sha256_ctx, sha256_bin);
    mbedtls_sha256_free(&s_sha256_ctx);
    s_sha256_active = false;

    if (ret != 0) {
        ESP_LOGE(TAG, "mbedtls_sha256_finish failed: %d", ret);
        return ESP_FAIL;
    }

    /* Convert binary hash to hex string */
    char sha256_hex[65];
    for (int i = 0; i < 32; i++) {
        snprintf(&sha256_hex[i * 2], 3, "%02x", sha256_bin[i]);
    }

    /* Case-insensitive comparison */
    for (int i = 0; i < 64; i++) {
        if (tolower((unsigned char)sha256_hex[i]) != tolower((unsigned char)expected_sha256[i])) {
            ESP_LOGE(TAG, "SHA256 mismatch");
            ESP_LOGE(TAG, "  expected: %.64s", expected_sha256);
            ESP_LOGE(TAG, "  computed: %s", sha256_hex);
            return ESP_ERR_OTA_VALIDATE_FAILED;
        }
    }

    ESP_LOGI(TAG, "SHA256 verification passed");
    return ESP_OK;
}

esp_err_t ota_flash_end(void)
{
    esp_err_t err = esp_ota_end(g_ota.session.ota_handle);
    g_ota.session.ota_handle = 0;  // Handle consumed by esp_ota_end regardless of result
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
        g_ota.session.update_partition = NULL;
        return err;
    }

    err = esp_ota_set_boot_partition(g_ota.session.update_partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
        g_ota.session.update_partition = NULL;
        return err;
    }

    ESP_LOGI(TAG, "OTA flash end: boot partition set to %s",
             g_ota.session.update_partition->label);

    g_ota.session.update_partition = NULL;
    return ESP_OK;
}

esp_err_t ota_flash_abort(void)
{
    esp_err_t result = ESP_OK;

    if (g_ota.session.ota_handle != 0) {
        esp_err_t err = esp_ota_abort(g_ota.session.ota_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "esp_ota_abort failed: %s (partition may be in inconsistent state)",
                     esp_err_to_name(err));
            result = err;
        }
        ESP_LOGW(TAG, "OTA flash aborted");
    }

    g_ota.session.ota_handle = 0;
    g_ota.session.update_partition = NULL;
    g_ota.session.bytes_written = 0;
    g_ota.session.total_bytes = 0;

    if (s_sha256_active) {
        mbedtls_sha256_free(&s_sha256_ctx);
        s_sha256_active = false;
    }

    return result;
}

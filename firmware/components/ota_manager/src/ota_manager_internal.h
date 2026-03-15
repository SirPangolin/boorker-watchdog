#pragma once

#include "ota_manager.h"
#include "esp_ota_ops.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

/**
 * @brief Release channel for OTA updates
 */
typedef enum {
    OTA_CHANNEL_STABLE = 0,     /**< Stable releases only */
    OTA_CHANNEL_PRERELEASE = 1, /**< Include pre-releases */
    OTA_CHANNEL_MAX             /**< Sentinel — must be last */
} ota_channel_t;

/**
 * @brief Active OTA flash session state (download or upload)
 */
typedef struct {
    esp_ota_handle_t ota_handle;
    const esp_partition_t *update_partition;
    uint32_t bytes_written;
    uint32_t total_bytes;
    ota_progress_cb_t progress_cb;
    void *progress_ctx;
    char expected_sha256[65];
    bool has_expected_sha256;
} ota_session_t;

// Internal state
typedef struct {
    bool initialized;
    SemaphoreHandle_t mutex;

    // Update info
    bool update_available;
    ota_update_info_t update_info;

    // Active flash session
    ota_session_t session;

    // Config (from NVS)
    char repo_url[128];        // GitHub repo URL override
    ota_channel_t channel;     // Release channel
    uint32_t last_check;       // Unix timestamp of last check

    // Background task
    TaskHandle_t check_task;
} ota_ctx_t;

// Global context (defined in ota_manager.c)
extern ota_ctx_t g_ota;

// Mutex helpers — OTA_LOCK() evaluates to true on success
#define OTA_MUTEX_TIMEOUT_MS 1000
#define OTA_LOCK()   (xSemaphoreTake(g_ota.mutex, pdMS_TO_TICKS(OTA_MUTEX_TIMEOUT_MS)) == pdTRUE)
#define OTA_UNLOCK() xSemaphoreGive(g_ota.mutex)

// Internal functions (implemented in other source files)
// ota_github.c — writes results to output params, caller copies under lock
esp_err_t ota_github_check_releases(ota_update_info_t *out_info, bool *out_available);
bool ota_github_version_newer(const char *current, const char *candidate);

// ota_download.c
esp_err_t ota_download_start(const ota_update_info_t *info, ota_progress_cb_t cb, void *ctx);

// ota_flash.c
esp_err_t ota_flash_begin(uint32_t image_size);
esp_err_t ota_flash_write(const void *data, size_t len);
esp_err_t ota_flash_verify_hash(const char *expected_sha256);
esp_err_t ota_flash_end(void);
esp_err_t ota_flash_abort(void);

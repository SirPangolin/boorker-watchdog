/**
 * @file ota_manager.c
 * @brief Core OTA manager state machine
 *
 * Manages OTA lifecycle: background update checks, GitHub download,
 * local upload sessions, and firmware validation.
 */

#include "ota_manager_internal.h"
#include "event_bus.h"
#include "system_state.h"
#include "version.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include <string.h>
#include <time.h>

static const char *TAG = "ota_manager";

// Global context
ota_ctx_t g_ota;

// NVS namespace and keys
#define OTA_NVS_NAMESPACE "ota"
#define OTA_NVS_KEY_REPO  "repo_url"
#define OTA_NVS_KEY_CHAN  "channel"
#define OTA_NVS_KEY_LAST  "last_check"

// Forward declarations
static void ota_check_task(void *arg);

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void load_nvs_config(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(OTA_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed (%s), using defaults", esp_err_to_name(err));
        strncpy(g_ota.repo_url, BOORKER_GITHUB_URL, sizeof(g_ota.repo_url) - 1);
        g_ota.repo_url[sizeof(g_ota.repo_url) - 1] = '\0';
        g_ota.channel = CONFIG_OTA_MANAGER_DEFAULT_CHANNEL;
        g_ota.last_check = 0;
        return;
    }

    size_t len = sizeof(g_ota.repo_url);
    err = nvs_get_str(nvs, OTA_NVS_KEY_REPO, g_ota.repo_url, &len);
    if (err != ESP_OK) {
        strncpy(g_ota.repo_url, BOORKER_GITHUB_URL, sizeof(g_ota.repo_url) - 1);
        g_ota.repo_url[sizeof(g_ota.repo_url) - 1] = '\0';
    }

    err = nvs_get_u8(nvs, OTA_NVS_KEY_CHAN, &g_ota.channel);
    if (err != ESP_OK) {
        g_ota.channel = CONFIG_OTA_MANAGER_DEFAULT_CHANNEL;
    }

    err = nvs_get_u32(nvs, OTA_NVS_KEY_LAST, &g_ota.last_check);
    if (err != ESP_OK) {
        g_ota.last_check = 0;
    }

    nvs_close(nvs);
    ESP_LOGI(TAG, "Config loaded: repo=%s channel=%u last_check=%lu",
             g_ota.repo_url, g_ota.channel, (unsigned long)g_ota.last_check);
}

static void save_last_check(uint32_t timestamp)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(OTA_NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err == ESP_OK) {
        nvs_set_u32(nvs, OTA_NVS_KEY_LAST, timestamp);
        nvs_commit(nvs);
        nvs_close(nvs);
    }
}

// ---------------------------------------------------------------------------
// Background task
// ---------------------------------------------------------------------------

static void ota_check_task(void *arg)
{
    (void)arg;
    const TickType_t interval = pdMS_TO_TICKS(
        (uint32_t)CONFIG_OTA_MANAGER_CHECK_INTERVAL_HOURS * 3600U * 1000U);

    for (;;) {
        // Wait one full interval before checking (REST API can trigger immediate)
        vTaskDelay(interval);
        ota_manager_check_now();
    }
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

// NOTE: Must be called from app_main() before other tasks access OTA manager
esp_err_t ota_manager_init(void)
{
    if (g_ota.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    g_ota.mutex = xSemaphoreCreateMutex();
    if (g_ota.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    g_ota.update_available = false;
    memset(&g_ota.update_info, 0, sizeof(g_ota.update_info));
    g_ota.ota_handle = 0;
    g_ota.update_partition = NULL;
    g_ota.bytes_written = 0;
    g_ota.total_bytes = 0;
    g_ota.progress_cb = NULL;
    g_ota.progress_ctx = NULL;
    g_ota.expected_sha256[0] = '\0';
    g_ota.has_expected_sha256 = false;
    g_ota.repo_url[0] = '\0';
    g_ota.channel = 0;
    g_ota.last_check = 0;
    g_ota.check_task = NULL;

    load_nvs_config();

    BaseType_t ret = xTaskCreate(
        ota_check_task,
        "ota_check",
        CONFIG_OTA_MANAGER_TASK_STACK_SIZE,
        NULL,
        CONFIG_OTA_MANAGER_TASK_PRIORITY,
        &g_ota.check_task);

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create check task");
        vSemaphoreDelete(g_ota.mutex);
        g_ota.mutex = NULL;
        return ESP_ERR_NO_MEM;
    }

    g_ota.initialized = true;
    ESP_LOGI(TAG, "Initialized (firmware %s)", BOORKER_VERSION_STRING);
    return ESP_OK;
}

esp_err_t ota_manager_check_now(void)
{
    if (!OTA_LOCK()) {
        return ESP_ERR_TIMEOUT;
    }

    if (!g_ota.initialized) {
        OTA_UNLOCK();
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Checking for updates...");
    esp_err_t err = ota_github_check_releases();

    if (err == ESP_OK && g_ota.update_available) {
        char msg[128];
        snprintf(msg, sizeof(msg), "Update available: v%s", g_ota.update_info.version);
        event_bus_post_motd("ota", msg, MOTD_PRIORITY_INFO);
        ESP_LOGI(TAG, "%s", msg);
    } else if (err == ESP_OK) {
        ESP_LOGI(TAG, "Firmware is up to date");
    } else {
        ESP_LOGW(TAG, "Update check failed: %s", esp_err_to_name(err));
    }

    system_state_set_ota(SYSTEM_OTA_IDLE);

    // Save last check timestamp
    g_ota.last_check = (uint32_t)time(NULL);
    save_last_check(g_ota.last_check);

    OTA_UNLOCK();
    return err;
}

// NOTE: Returns pointer to internal state — caller must not free or cache long-term
const ota_update_info_t *ota_manager_get_available_update(void)
{
    if (!OTA_LOCK()) {
        return NULL;
    }

    const ota_update_info_t *result = g_ota.update_available
        ? &g_ota.update_info
        : NULL;

    OTA_UNLOCK();
    return result;
}

esp_err_t ota_manager_start_update(ota_progress_cb_t cb, void *ctx)
{
    if (!OTA_LOCK()) {
        return ESP_ERR_TIMEOUT;
    }

    if (!g_ota.initialized) {
        OTA_UNLOCK();
        return ESP_ERR_INVALID_STATE;
    }

    if (system_state_get_ota() != SYSTEM_OTA_IDLE) {
        ESP_LOGE(TAG, "OTA already in progress");
        OTA_UNLOCK();
        return ESP_ERR_INVALID_STATE;
    }

    if (!g_ota.update_available) {
        ESP_LOGE(TAG, "No update available");
        OTA_UNLOCK();
        return ESP_ERR_INVALID_STATE;
    }

    system_state_set_ota(SYSTEM_OTA_DOWNLOADING);
    g_ota.progress_cb = cb;
    g_ota.progress_ctx = ctx;

    OTA_UNLOCK();

    // Blocking download — mutex released during network I/O
    esp_err_t err = ota_download_start(&g_ota.update_info, cb, ctx);

    OTA_LOCK();  // re-acquire after blocking call

    if (err == ESP_OK) {
        system_state_set_ota(SYSTEM_OTA_PENDING_REBOOT);
        event_bus_post_motd("ota", "Firmware updated, reboot required",
                            MOTD_PRIORITY_WARNING);
        ESP_LOGI(TAG, "Update complete, pending reboot");
    } else {
        system_state_set_ota(SYSTEM_OTA_IDLE);
        ota_flash_abort();
        ESP_LOGE(TAG, "Update failed: %s", esp_err_to_name(err));
    }

    OTA_UNLOCK();
    return err;
}

esp_err_t ota_manager_start_upload(uint32_t size, const char *expected_sha256,
                                    ota_progress_cb_t cb, void *ctx)
{
    if (!OTA_LOCK()) {
        return ESP_ERR_TIMEOUT;
    }

    if (!g_ota.initialized) {
        OTA_UNLOCK();
        return ESP_ERR_INVALID_STATE;
    }

    if (system_state_get_ota() != SYSTEM_OTA_IDLE) {
        ESP_LOGE(TAG, "OTA already in progress");
        OTA_UNLOCK();
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ota_flash_begin(size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Flash begin failed: %s", esp_err_to_name(err));
        OTA_UNLOCK();
        return err;
    }

    system_state_set_ota(SYSTEM_OTA_DOWNLOADING);
    g_ota.total_bytes = size;
    g_ota.bytes_written = 0;
    g_ota.progress_cb = cb;
    g_ota.progress_ctx = ctx;

    if (expected_sha256 != NULL) {
        strncpy(g_ota.expected_sha256, expected_sha256,
                sizeof(g_ota.expected_sha256) - 1);
        g_ota.expected_sha256[sizeof(g_ota.expected_sha256) - 1] = '\0';
        g_ota.has_expected_sha256 = true;
    } else {
        g_ota.expected_sha256[0] = '\0';
        g_ota.has_expected_sha256 = false;
    }

    ESP_LOGI(TAG, "Upload session started (size=%lu, sha256=%s)",
             (unsigned long)size,
             g_ota.has_expected_sha256 ? "provided" : "none");

    OTA_UNLOCK();
    return ESP_OK;
}

esp_err_t ota_manager_write_upload_chunk(const void *data, size_t len)
{
    if (!OTA_LOCK()) {
        return ESP_ERR_TIMEOUT;
    }

    if (system_state_get_ota() != SYSTEM_OTA_DOWNLOADING) {
        OTA_UNLOCK();
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t err = ota_flash_write(data, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Flash write failed: %s", esp_err_to_name(err));
        OTA_UNLOCK();
        return err;
    }

    // Copy callback info under lock, invoke outside to avoid holding mutex
    ota_progress_cb_t cb = g_ota.progress_cb;
    void *cb_ctx = g_ota.progress_ctx;
    uint32_t written = g_ota.bytes_written;
    uint32_t total = g_ota.total_bytes;

    OTA_UNLOCK();

    if (cb) {
        cb(written, total, cb_ctx);
    }

    return ESP_OK;
}

esp_err_t ota_manager_finish_upload(void)
{
    if (system_state_get_ota() != SYSTEM_OTA_DOWNLOADING) {
        return ESP_ERR_INVALID_STATE;
    }

    system_state_set_ota(SYSTEM_OTA_VERIFYING);

    if (g_ota.has_expected_sha256) {
        esp_err_t err = ota_flash_verify_hash(g_ota.expected_sha256);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SHA-256 verification failed");
            system_state_set_ota(SYSTEM_OTA_IDLE);
            ota_flash_abort();
            return err;
        }
    }

    esp_err_t err = ota_flash_end();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Flash finalize failed: %s", esp_err_to_name(err));
        system_state_set_ota(SYSTEM_OTA_IDLE);
        return err;
    }

    system_state_set_ota(SYSTEM_OTA_PENDING_REBOOT);
    event_bus_post_motd("ota", "Firmware uploaded, reboot required",
                        MOTD_PRIORITY_WARNING);
    ESP_LOGI(TAG, "Upload complete, pending reboot");

    return ESP_OK;
}

esp_err_t ota_manager_abort(void)
{
    if (!g_ota.initialized) {
        return ESP_OK;
    }

    esp_err_t err = ota_flash_abort();

    event_bus_clear_motds_from("ota");
    system_state_set_ota(SYSTEM_OTA_IDLE);

    g_ota.progress_cb = NULL;
    g_ota.progress_ctx = NULL;
    g_ota.bytes_written = 0;
    g_ota.total_bytes = 0;

    ESP_LOGI(TAG, "OTA aborted");
    return err;
}

esp_err_t ota_manager_mark_valid(void)
{
    esp_err_t err = esp_ota_mark_app_valid_cancel_rollback();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Firmware marked as valid");
    } else {
        ESP_LOGE(TAG, "Failed to mark firmware valid: %s", esp_err_to_name(err));
    }
    return err;
}

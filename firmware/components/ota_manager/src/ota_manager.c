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

static uint8_t ota_get_state(void)
{
    return system_state_get()->ota.state;
}

static void ota_set_state(uint8_t state)
{
    system_state_t ss;
    if (system_state_copy(&ss) == ESP_OK) {
        ss.ota.state = state;
        esp_err_t ret = system_state_set_ota(&ss.ota);
        if (ret != ESP_OK) {
            ESP_LOGW(TAG, "Failed to set OTA state to %s: %s",
                     system_ota_state_name(state), esp_err_to_name(ret));
        }
    }
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static void load_nvs_config(void)
{
    nvs_handle_t nvs;
    esp_err_t err = nvs_open(OTA_NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "NVS open failed (%s), using defaults", esp_err_to_name(err));
        strncpy(g_ota.repo_url, BOORKER_GITHUB_URL, sizeof(g_ota.repo_url) - 1);
        g_ota.repo_url[sizeof(g_ota.repo_url) - 1] = '\0';
        g_ota.channel = CONFIG_OTA_MANAGER_DEFAULT_CHANNEL;
        g_ota.last_check = 0;
        return;
    }

    size_t len = sizeof(g_ota.repo_url);
    err = nvs_get_str(nvs, OTA_NVS_KEY_REPO, g_ota.repo_url, &len);
    if (err != ESP_OK) {
        ESP_LOGD(TAG, "NVS key '%s' not found, using default repo", OTA_NVS_KEY_REPO);
        strncpy(g_ota.repo_url, BOORKER_GITHUB_URL, sizeof(g_ota.repo_url) - 1);
        g_ota.repo_url[sizeof(g_ota.repo_url) - 1] = '\0';
    }

    uint8_t channel_raw;
    err = nvs_get_u8(nvs, OTA_NVS_KEY_CHAN, &channel_raw);
    if (err != ESP_OK || channel_raw >= OTA_CHANNEL_MAX) {
        if (err == ESP_OK) {
            ESP_LOGW(TAG, "Invalid channel value %u in NVS, using default", channel_raw);
        }
        g_ota.channel = CONFIG_OTA_MANAGER_DEFAULT_CHANNEL;
    } else {
        g_ota.channel = (ota_channel_t)channel_raw;
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
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS open for last_check failed: %s", esp_err_to_name(err));
        return;
    }
    err = nvs_set_u32(nvs, OTA_NVS_KEY_LAST, timestamp);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS set last_check failed: %s", esp_err_to_name(err));
    }
    err = nvs_commit(nvs);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS commit last_check failed: %s", esp_err_to_name(err));
    }
    nvs_close(nvs);
}

// ---------------------------------------------------------------------------
// Background task
// ---------------------------------------------------------------------------

static void ota_check_task(void *arg)
{
    (void)arg;

    /* Sleep in 1-hour increments to avoid TickType_t overflow on long intervals.
     * CONFIG_OTA_MANAGER_CHECK_INTERVAL_HOURS can be up to 168 (1 week). */
    const TickType_t one_hour = pdMS_TO_TICKS(3600U * 1000U);

    for (;;) {
        for (int h = 0; h < CONFIG_OTA_MANAGER_CHECK_INTERVAL_HOURS; h++) {
            vTaskDelay(one_hour);
        }
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
    memset(&g_ota.session, 0, sizeof(g_ota.session));
    g_ota.repo_url[0] = '\0';
    g_ota.channel = OTA_CHANNEL_STABLE;
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

    /* Release mutex before blocking HTTPS call */
    OTA_UNLOCK();

    /* Write to local vars — caller copies into g_ota under lock */
    ota_update_info_t check_info;
    bool check_available = false;
    esp_err_t err = ota_github_check_releases(&check_info, &check_available);

    if (!OTA_LOCK()) {
        ESP_LOGE(TAG, "Failed to re-acquire lock after update check");
        return (err == ESP_OK) ? ESP_ERR_TIMEOUT : err;
    }

    /* Copy results into global state under lock */
    if (err == ESP_OK) {
        g_ota.update_available = check_available;
        if (check_available) {
            memcpy(&g_ota.update_info, &check_info, sizeof(g_ota.update_info));
        }
    }

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

    ota_set_state(SYSTEM_OTA_IDLE);

    /* Only persist last_check timestamp on success */
    if (err == ESP_OK) {
        g_ota.last_check = (uint32_t)time(NULL);
        save_last_check(g_ota.last_check);
    }

    OTA_UNLOCK();
    return err;
}

esp_err_t ota_manager_get_available_update(ota_update_info_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!OTA_LOCK()) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret;
    if (g_ota.update_available) {
        memcpy(out, &g_ota.update_info, sizeof(*out));
        ret = ESP_OK;
    } else {
        ret = ESP_ERR_NOT_FOUND;
    }

    OTA_UNLOCK();
    return ret;
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

    if (ota_get_state() != SYSTEM_OTA_IDLE) {
        ESP_LOGE(TAG, "OTA already in progress");
        OTA_UNLOCK();
        return ESP_ERR_INVALID_STATE;
    }

    if (!g_ota.update_available) {
        ESP_LOGE(TAG, "No update available");
        OTA_UNLOCK();
        return ESP_ERR_INVALID_STATE;
    }

    ota_set_state(SYSTEM_OTA_DOWNLOADING);
    g_ota.session.progress_cb = cb;
    g_ota.session.progress_ctx = ctx;

    /* Copy update info under lock — download runs without mutex */
    ota_update_info_t info;
    memcpy(&info, &g_ota.update_info, sizeof(info));

    OTA_UNLOCK();

    /* Blocking download — mutex released during network I/O.
     * ota_download_start owns abort-on-failure internally. */
    esp_err_t err = ota_download_start(&info, cb, ctx);

    if (!OTA_LOCK()) {
        ESP_LOGE(TAG, "Failed to re-acquire lock after download");
        return (err == ESP_OK) ? ESP_ERR_TIMEOUT : err;
    }

    if (err == ESP_OK) {
        ota_set_state(SYSTEM_OTA_PENDING_REBOOT);
        event_bus_post_motd("ota", "Firmware updated, reboot required",
                            MOTD_PRIORITY_WARNING);
        ESP_LOGI(TAG, "Update complete, pending reboot");
    } else {
        ota_set_state(SYSTEM_OTA_IDLE);
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

    if (ota_get_state() != SYSTEM_OTA_IDLE) {
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

    ota_set_state(SYSTEM_OTA_DOWNLOADING);
    g_ota.session.total_bytes = size;
    g_ota.session.bytes_written = 0;
    g_ota.session.progress_cb = cb;
    g_ota.session.progress_ctx = ctx;

    if (expected_sha256 != NULL) {
        strncpy(g_ota.session.expected_sha256, expected_sha256,
                sizeof(g_ota.session.expected_sha256) - 1);
        g_ota.session.expected_sha256[sizeof(g_ota.session.expected_sha256) - 1] = '\0';
        g_ota.session.has_expected_sha256 = true;
    } else {
        g_ota.session.expected_sha256[0] = '\0';
        g_ota.session.has_expected_sha256 = false;
    }

    ESP_LOGI(TAG, "Upload session started (size=%lu, sha256=%s)",
             (unsigned long)size,
             g_ota.session.has_expected_sha256 ? "provided" : "none");

    OTA_UNLOCK();
    return ESP_OK;
}

esp_err_t ota_manager_write_upload_chunk(const void *data, size_t len)
{
    if (!OTA_LOCK()) {
        return ESP_ERR_TIMEOUT;
    }

    if (ota_get_state() != SYSTEM_OTA_DOWNLOADING) {
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
    ota_progress_cb_t cb = g_ota.session.progress_cb;
    void *cb_ctx = g_ota.session.progress_ctx;
    uint32_t written = g_ota.session.bytes_written;
    uint32_t total = g_ota.session.total_bytes;

    OTA_UNLOCK();

    if (cb) {
        cb(written, total, cb_ctx);
    }

    return ESP_OK;
}

esp_err_t ota_manager_finish_upload(void)
{
    if (!OTA_LOCK()) {
        return ESP_ERR_TIMEOUT;
    }

    if (ota_get_state() != SYSTEM_OTA_DOWNLOADING) {
        OTA_UNLOCK();
        return ESP_ERR_INVALID_STATE;
    }

    ota_set_state(SYSTEM_OTA_VERIFYING);

    // Copy SHA256 state under lock, then release for blocking flash ops
    bool has_sha256 = g_ota.session.has_expected_sha256;
    char sha256_copy[65];
    if (has_sha256) {
        memcpy(sha256_copy, g_ota.session.expected_sha256, sizeof(sha256_copy));
    }

    OTA_UNLOCK();

    /* Guard: abort() may have cleared the session while we released the lock */
    if (g_ota.session.ota_handle == 0) {
        ESP_LOGW(TAG, "Flash session aborted during finalization");
        ota_set_state(SYSTEM_OTA_IDLE);
        return ESP_ERR_INVALID_STATE;
    }

    if (has_sha256) {
        esp_err_t err = ota_flash_verify_hash(sha256_copy);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "SHA-256 verification failed");
            ota_set_state(SYSTEM_OTA_IDLE);
            ota_flash_abort();
            return err;
        }
    }

    esp_err_t err = ota_flash_end();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Flash finalize failed: %s", esp_err_to_name(err));
        ota_set_state(SYSTEM_OTA_IDLE);
        return err;
    }

    ota_set_state(SYSTEM_OTA_PENDING_REBOOT);
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

    bool locked = OTA_LOCK();
    if (!locked) {
        ESP_LOGW(TAG, "Abort: failed to acquire mutex, proceeding without lock");
    }

    esp_err_t err = ota_flash_abort();

    event_bus_clear_motds_from("ota");
    ota_set_state(SYSTEM_OTA_IDLE);

    memset(&g_ota.session, 0, sizeof(g_ota.session));

    if (locked) {
        OTA_UNLOCK();
    }

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

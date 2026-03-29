#include "system_state.h"
#include "event_bus.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "system_state";

#define MUTEX_TIMEOUT_MS        100
#define DEINIT_MUTEX_TIMEOUT_MS 500
#define NVS_NAMESPACE           "sys_state"
#define NVS_KEY_CLAIMED         "claimed"

static struct {
    bool initialized;
    SemaphoreHandle_t mutex;
    system_state_t state;
} s_ctx = {0};

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

static esp_err_t notify_section(system_state_section_t section)
{
    event_notify_t event = {
        .type = EVENT_NOTIFY_SYSTEM_STATE,
        .system_state = { .section = (uint8_t)section },
    };
    return event_bus_notify(&event);
}

static esp_err_t load_claimed_from_nvs(void)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_ctx.state.claimed = false;
        return ESP_OK;
    }
    if (err != ESP_OK) return err;

    uint8_t val = 0;
    err = nvs_get_u8(handle, NVS_KEY_CLAIMED, &val);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        s_ctx.state.claimed = false;
        err = ESP_OK;
    } else if (err == ESP_OK) {
        s_ctx.state.claimed = (val != 0);
    }

    nvs_close(handle);
    return err;
}

static esp_err_t save_claimed_to_nvs(bool claimed)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return err;

    err = nvs_set_u8(handle, NVS_KEY_CLAIMED, claimed ? 1 : 0);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }

    nvs_close(handle);
    return err;
}

// ---------------------------------------------------------------------------
// Init / Deinit
// ---------------------------------------------------------------------------

esp_err_t system_state_init(void)
{
    if (s_ctx.initialized) return ESP_OK;

    s_ctx.mutex = xSemaphoreCreateMutex();
    if (s_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    memset(&s_ctx.state, 0, sizeof(s_ctx.state));

    esp_err_t err = load_claimed_from_nvs();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to load claimed state: %s", esp_err_to_name(err));
    }

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "Initialized (claimed=%d)", s_ctx.state.claimed);
    return ESP_OK;
}

esp_err_t system_state_deinit(void)
{
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(DEINIT_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_ctx.initialized = false;
    xSemaphoreGive(s_ctx.mutex);
    vSemaphoreDelete(s_ctx.mutex);
    s_ctx.mutex = NULL;

    return ESP_OK;
}

// ---------------------------------------------------------------------------
// Getter
// ---------------------------------------------------------------------------

const system_state_t *system_state_get(void)
{
    return &s_ctx.state;
}

bool system_state_is_claimed(void)
{
    return s_ctx.initialized ? s_ctx.state.claimed : false;
}

// ---------------------------------------------------------------------------
// Section setters
// ---------------------------------------------------------------------------

esp_err_t system_state_set_identity(const char *node_name, const char *node_suffix)
{
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;

    if (node_name) {
        strncpy(s_ctx.state.node_name, node_name, sizeof(s_ctx.state.node_name) - 1);
    }
    if (node_suffix) {
        strncpy(s_ctx.state.node_suffix, node_suffix, sizeof(s_ctx.state.node_suffix) - 1);
    }

    xSemaphoreGive(s_ctx.mutex);
    notify_section(SYSTEM_STATE_IDENTITY_UPDATED);
    return ESP_OK;
}

esp_err_t system_state_set_claimed(bool claimed)
{
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;

    s_ctx.state.claimed = claimed;
    xSemaphoreGive(s_ctx.mutex);

    esp_err_t err = save_claimed_to_nvs(claimed);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to persist claimed=%d: %s", claimed, esp_err_to_name(err));
    }

    notify_section(SYSTEM_STATE_LIFECYCLE_UPDATED);
    return ESP_OK;
}

esp_err_t system_state_set_ota(const system_ota_t *ota)
{
    if (!s_ctx.initialized || !ota) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;

    s_ctx.state.ota = *ota;

    xSemaphoreGive(s_ctx.mutex);
    notify_section(SYSTEM_STATE_OTA_UPDATED);
    return ESP_OK;
}

esp_err_t system_state_set_reboot(bool pending, uint32_t remaining_seconds)
{
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;

    s_ctx.state.reboot.pending = pending;
    s_ctx.state.reboot.remaining_seconds = remaining_seconds;

    xSemaphoreGive(s_ctx.mutex);
    notify_section(SYSTEM_STATE_REBOOT_UPDATED);
    return ESP_OK;
}

esp_err_t system_state_set_wifi(const system_wifi_t *wifi)
{
    if (!s_ctx.initialized || !wifi) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;

    s_ctx.state.wifi = *wifi;

    xSemaphoreGive(s_ctx.mutex);
    notify_section(SYSTEM_STATE_WIFI_UPDATED);
    return ESP_OK;
}

esp_err_t system_state_set_lora(const system_lora_t *lora)
{
    if (!s_ctx.initialized || !lora) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;

    s_ctx.state.lora = *lora;

    xSemaphoreGive(s_ctx.mutex);
    notify_section(SYSTEM_STATE_LORA_UPDATED);
    return ESP_OK;
}

esp_err_t system_state_set_sensors(const system_sensors_t *sensors)
{
    if (!s_ctx.initialized || !sensors) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;

    s_ctx.state.sensors = *sensors;

    xSemaphoreGive(s_ctx.mutex);
    notify_section(SYSTEM_STATE_SENSORS_UPDATED);
    return ESP_OK;
}

esp_err_t system_state_set_led(const system_led_t *led)
{
    if (!s_ctx.initialized || !led) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;

    s_ctx.state.led = *led;

    xSemaphoreGive(s_ctx.mutex);
    notify_section(SYSTEM_STATE_PERIPHERALS_UPDATED);
    return ESP_OK;
}

esp_err_t system_state_set_buzzer(const system_buzzer_t *buzzer)
{
    if (!s_ctx.initialized || !buzzer) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;

    s_ctx.state.buzzer = *buzzer;

    xSemaphoreGive(s_ctx.mutex);
    notify_section(SYSTEM_STATE_PERIPHERALS_UPDATED);
    return ESP_OK;
}

esp_err_t system_state_set_display(const system_display_t *display)
{
    if (!s_ctx.initialized || !display) return ESP_ERR_INVALID_ARG;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;

    s_ctx.state.display = *display;

    xSemaphoreGive(s_ctx.mutex);
    notify_section(SYSTEM_STATE_PERIPHERALS_UPDATED);
    return ESP_OK;
}

esp_err_t system_state_set_buttons(uint8_t count)
{
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;

    s_ctx.state.buttons.count = count;

    xSemaphoreGive(s_ctx.mutex);
    notify_section(SYSTEM_STATE_PERIPHERALS_UPDATED);
    return ESP_OK;
}

esp_err_t system_state_set_system(const char *firmware_version, const char *idf_version,
                                   uint8_t chip_major, uint8_t chip_minor, uint8_t chip_cores,
                                   uint32_t heap_free, uint32_t heap_total,
                                   uint32_t psram_free, uint32_t psram_total,
                                   int64_t uptime_us)
{
    if (!s_ctx.initialized) return ESP_ERR_INVALID_STATE;
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) return ESP_ERR_TIMEOUT;

    if (firmware_version) {
        strncpy(s_ctx.state.firmware_version, firmware_version, sizeof(s_ctx.state.firmware_version) - 1);
    }
    if (idf_version) {
        strncpy(s_ctx.state.idf_version, idf_version, sizeof(s_ctx.state.idf_version) - 1);
    }
    s_ctx.state.chip_revision_major = chip_major;
    s_ctx.state.chip_revision_minor = chip_minor;
    s_ctx.state.chip_cores = chip_cores;
    s_ctx.state.heap_free = heap_free;
    s_ctx.state.heap_total = heap_total;
    s_ctx.state.psram_free = psram_free;
    s_ctx.state.psram_total = psram_total;
    s_ctx.state.uptime_us = uptime_us;

    xSemaphoreGive(s_ctx.mutex);
    notify_section(SYSTEM_STATE_SYSTEM_UPDATED);
    return ESP_OK;
}

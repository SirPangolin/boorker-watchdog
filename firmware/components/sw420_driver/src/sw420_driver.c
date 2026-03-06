/**
 * @file sw420_driver.c
 * @brief SW-420 vibration sensor driver implementation
 */

#include "sw420_driver.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "sdkconfig.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "sw420_driver";

#define NVS_NAMESPACE "sensors"
#define NVS_KEY_ON_MS "sw420_on_ms"
#define NVS_KEY_OFF_MS "sw420_off_ms"

typedef struct sw420_inst {
    gpio_num_t gpio;
    bool active_high;

    // Debounce state
    bool current_state;
    bool raw_state;
    uint32_t state_start_ms;

    // Config
    uint32_t debounce_on_ms;
    uint32_t debounce_off_ms;
} sw420_inst_t;

// v1: Single instance limit
static sw420_handle_t s_single_instance = NULL;

esp_err_t sw420_driver_create(gpio_num_t gpio, sw420_handle_t *out_handle)
{
    if (out_handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!GPIO_IS_VALID_GPIO(gpio)) {
        ESP_LOGE(TAG, "Invalid GPIO: %d", gpio);
        return ESP_ERR_INVALID_ARG;
    }

    if (s_single_instance != NULL) {
        ESP_LOGE(TAG, "Multi-instance not yet supported");
        return ESP_ERR_INVALID_STATE;
    }

    sw420_inst_t *inst = calloc(1, sizeof(sw420_inst_t));
    if (inst == NULL) {
        ESP_LOGE(TAG, "Failed to allocate instance");
        return ESP_ERR_NO_MEM;
    }

    inst->gpio = gpio;
    inst->active_high = CONFIG_SW420_ACTIVE_HIGH;
    inst->debounce_on_ms = CONFIG_SW420_DEBOUNCE_ON_MS;
    inst->debounce_off_ms = CONFIG_SW420_DEBOUNCE_OFF_MS;
    inst->current_state = false;
    inst->raw_state = false;
    inst->state_start_ms = 0;

    // Configure GPIO as input
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(err));
        free(inst);
        return err;
    }

    // Load config from NVS (ignores errors, uses defaults)
    sw420_driver_load_config(inst);

    s_single_instance = inst;
    *out_handle = inst;

    ESP_LOGI(TAG, "SW-420 driver created on GPIO %d (active_%s)",
             gpio, inst->active_high ? "high" : "low");
    return ESP_OK;
}

esp_err_t sw420_driver_destroy(sw420_handle_t handle)
{
    if (handle == NULL) {
        return ESP_OK;
    }

    sw420_inst_t *inst = (sw420_inst_t *)handle;

    ESP_LOGI(TAG, "Destroying SW-420 driver on GPIO %d", inst->gpio);

    // Reset GPIO
    gpio_reset_pin(inst->gpio);

    free(inst);
    s_single_instance = NULL;

    return ESP_OK;
}

bool sw420_driver_read_raw(sw420_handle_t handle)
{
    if (handle == NULL) {
        return false;
    }

    sw420_inst_t *inst = (sw420_inst_t *)handle;
    int level = gpio_get_level(inst->gpio);

    return inst->active_high ? (level == 1) : (level == 0);
}

esp_err_t sw420_driver_read(sw420_handle_t handle, bool *vibrating)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (vibrating == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    sw420_inst_t *inst = (sw420_inst_t *)handle;
    uint32_t now_ms = xTaskGetTickCount() * portTICK_PERIOD_MS;

    bool raw_now = sw420_driver_read_raw(handle);

    // Detect raw state change
    if (raw_now != inst->raw_state) {
        inst->raw_state = raw_now;
        inst->state_start_ms = now_ms;
    }

    uint32_t elapsed_ms = now_ms - inst->state_start_ms;

    // Apply debounce logic (truth table from design)
    if (inst->current_state) {
        // Currently vibrating - check for stop
        if (!raw_now && elapsed_ms >= inst->debounce_off_ms) {
            inst->current_state = false;
            ESP_LOGD(TAG, "Transition: VIBRATING -> IDLE");
        }
    } else {
        // Currently idle - check for start
        if (raw_now && elapsed_ms >= inst->debounce_on_ms) {
            inst->current_state = true;
            ESP_LOGD(TAG, "Transition: IDLE -> VIBRATING");
        }
    }

    *vibrating = inst->current_state;
    return ESP_OK;
}

esp_err_t sw420_driver_set_config(sw420_handle_t handle,
                                   uint32_t debounce_on_ms,
                                   uint32_t debounce_off_ms)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    sw420_inst_t *inst = (sw420_inst_t *)handle;
    inst->debounce_on_ms = debounce_on_ms;
    inst->debounce_off_ms = debounce_off_ms;

    ESP_LOGI(TAG, "Config updated: on_ms=%lu, off_ms=%lu",
             (unsigned long)debounce_on_ms, (unsigned long)debounce_off_ms);
    return ESP_OK;
}

esp_err_t sw420_driver_get_config(sw420_handle_t handle,
                                   uint32_t *debounce_on_ms,
                                   uint32_t *debounce_off_ms)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    sw420_inst_t *inst = (sw420_inst_t *)handle;

    if (debounce_on_ms) *debounce_on_ms = inst->debounce_on_ms;
    if (debounce_off_ms) *debounce_off_ms = inst->debounce_off_ms;

    return ESP_OK;
}

esp_err_t sw420_driver_save_config(sw420_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    sw420_inst_t *inst = (sw420_inst_t *)handle;
    nvs_handle_t nvs;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u32(nvs, NVS_KEY_ON_MS, inst->debounce_on_ms);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS set %s failed: %s", NVS_KEY_ON_MS, esp_err_to_name(err));
        nvs_close(nvs);
        return err;
    }

    err = nvs_set_u32(nvs, NVS_KEY_OFF_MS, inst->debounce_off_ms);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS set %s failed: %s", NVS_KEY_OFF_MS, esp_err_to_name(err));
        nvs_close(nvs);
        return err;
    }

    err = nvs_commit(nvs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS commit failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG, "Config saved to NVS");
    }

    nvs_close(nvs);
    return err;
}

esp_err_t sw420_driver_load_config(sw420_handle_t handle)
{
    if (handle == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    sw420_inst_t *inst = (sw420_inst_t *)handle;
    nvs_handle_t nvs;

    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (err != ESP_OK) {
        // No saved config, use defaults - this is expected on first boot
        ESP_LOGD(TAG, "No saved config (using defaults): %s", esp_err_to_name(err));
        return ESP_OK;
    }

    // Load values, keep defaults if keys missing
    err = nvs_get_u32(nvs, NVS_KEY_ON_MS, &inst->debounce_on_ms);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS get %s failed: %s", NVS_KEY_ON_MS, esp_err_to_name(err));
    }

    err = nvs_get_u32(nvs, NVS_KEY_OFF_MS, &inst->debounce_off_ms);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS get %s failed: %s", NVS_KEY_OFF_MS, esp_err_to_name(err));
    }

    nvs_close(nvs);

    ESP_LOGI(TAG, "Config loaded: on_ms=%lu, off_ms=%lu",
             (unsigned long)inst->debounce_on_ms,
             (unsigned long)inst->debounce_off_ms);
    return ESP_OK;
}

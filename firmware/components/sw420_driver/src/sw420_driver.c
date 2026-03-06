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

    if (s_single_instance != NULL) {
        ESP_LOGE(TAG, "Multi-instance not yet supported");
        return ESP_ERR_INVALID_STATE;
    }

    // TODO: Implement
    ESP_LOGI(TAG, "sw420_driver_create stub called");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t sw420_driver_destroy(sw420_handle_t handle)
{
    // TODO: Implement
    return ESP_OK;
}

esp_err_t sw420_driver_read(sw420_handle_t handle, bool *vibrating)
{
    // TODO: Implement
    return ESP_ERR_NOT_SUPPORTED;
}

bool sw420_driver_read_raw(sw420_handle_t handle)
{
    // TODO: Implement
    return false;
}

esp_err_t sw420_driver_set_config(sw420_handle_t handle,
                                   uint32_t debounce_on_ms,
                                   uint32_t debounce_off_ms)
{
    // TODO: Implement
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t sw420_driver_get_config(sw420_handle_t handle,
                                   uint32_t *debounce_on_ms,
                                   uint32_t *debounce_off_ms)
{
    // TODO: Implement
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t sw420_driver_save_config(sw420_handle_t handle)
{
    // TODO: Implement
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t sw420_driver_load_config(sw420_handle_t handle)
{
    // TODO: Implement
    return ESP_ERR_NOT_SUPPORTED;
}

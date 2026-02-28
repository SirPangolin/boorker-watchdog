#pragma once

#include <stdbool.h>
#include "esp_err.h"

/**
 * @brief Start BLE provisioning
 * @param device_name Device name for BLE advertising
 * @return ESP_OK on success
 */
esp_err_t wifi_prov_start(const char *device_name);

/**
 * @brief Stop BLE provisioning
 */
void wifi_prov_stop(void);

/**
 * @brief Check if provisioning is active
 */
bool wifi_prov_is_active(void);

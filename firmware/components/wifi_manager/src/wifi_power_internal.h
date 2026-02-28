#pragma once

#include "esp_err.h"

/**
 * @brief Enable WiFi power saving mode
 * @return ESP_OK on success
 */
esp_err_t wifi_power_enable(void);

/**
 * @brief Disable WiFi power saving mode
 * @return ESP_OK on success
 */
esp_err_t wifi_power_disable(void);

#pragma once

#include "esp_err.h"

/**
 * @brief Start mDNS service
 * @param hostname Hostname for .local resolution
 * @return ESP_OK on success
 */
esp_err_t wifi_mdns_start(const char *hostname);

/**
 * @brief Stop mDNS service
 */
void wifi_mdns_stop(void);

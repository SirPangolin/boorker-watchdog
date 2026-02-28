#pragma once

#include "esp_err.h"
#include <stdbool.h>

esp_err_t ts_nvs_store_key(const char *key);
esp_err_t ts_nvs_load_key(char *buf, size_t buf_len);
esp_err_t ts_nvs_clear_key(void);
bool ts_nvs_has_key(void);

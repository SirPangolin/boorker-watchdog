/* Mock sensor_manager.h — redirects to mock_esp.h */
#pragma once
#include "mock_esp.h"
size_t sensor_manager_get_sensor_count(void);
esp_err_t sensor_manager_get_reading_by_index(size_t index, sensor_reading_t *out);
const char *sensor_manager_get_sensor_id(size_t index);

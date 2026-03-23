/* Mock esp_timer.h — redirects to mock_esp.h */
#pragma once
#include "mock_esp.h"
int64_t esp_timer_get_time(void);

/* Mock esp_system.h — redirects to mock_esp.h */
#pragma once
#include "mock_esp.h"
uint32_t esp_get_free_heap_size(void);

/**
 * @file mock_esp.h
 * @brief Mock ESP-IDF functions for desktop OLED emulator
 *
 * Types come from real firmware headers (system_state.h, event_bus.h, secrets.h).
 * This file provides mock function declarations and ESP-IDF stubs.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdarg.h>

#include "esp_err.h"
#include "system_state.h"
#include "secrets.h"

// Mock function declarations
const secrets_t *secrets_get(void);
const system_state_t *system_state_get(void);

// Reading cache (provided by mock, consumed by display_screens.c)
size_t display_get_reading_count(void);
bool display_get_reading(size_t index, const char **sensor_id,
                         float *value, float *value2, uint8_t *status);

// ESP log stub
void esp_log_write(int level, const char *tag, const char *fmt, ...);

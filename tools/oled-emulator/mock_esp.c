/**
 * @file mock_esp.c
 * @brief Mock implementations for desktop emulator
 *
 * All device state lives in one system_state_t struct.
 * Sensor readings provided via display_get_reading API.
 */

#include "mock_esp.h"
#include "event_bus.h"
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

// ---------------------------------------------------------------------------
// Mock system state
// ---------------------------------------------------------------------------

static system_state_t mock_state = {
    .node_name = "boorker-MOCK",
    .node_suffix = "MOCK",
    .claimed = true,
    .ota = { .state = SYSTEM_OTA_IDLE },
    .wifi = {
        .connected = true,
        .ssid = "MockNetwork",
        .rssi = -42,
        .ip = "192.168.68.54",
        .mac = { 0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01 },
    },
    .lora = {
        .frequency_mhz = 915,
        .tx_power_dbm = 22,
    },
    .firmware_version = "0.11.0-emu",
    .idf_version = "v5.5.3",
    .chip_revision_major = 0,
    .chip_revision_minor = 2,
    .chip_cores = 2,
    .heap_free = 161892,
    .heap_total = 327680,
};

const system_state_t *system_state_get(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
        mock_state.uptime_us = (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
    }
    return &mock_state;
}

// ---------------------------------------------------------------------------
// Mock secrets
// ---------------------------------------------------------------------------

static secrets_t mock_secrets = {
    .node_name = "boorker-MOCK",
    .web_password = "mock-web-pass",
    .ap_password = "mock-ap-pass",
    .ble_pop = "000000",
    .node_suffix = "MOCK",
};

const secrets_t *secrets_get(void)
{
    return &mock_secrets;
}

// ---------------------------------------------------------------------------
// Mock sensor readings (display cache API)
// ---------------------------------------------------------------------------

typedef struct {
    const char *sensor_id;
    float value;
    float value2;
    uint8_t status;
} mock_reading_t;

static mock_reading_t mock_readings[] = {
    { "temp_humidity", 73.8f, 55.2f, EVENT_SENSOR_ONLINE },
    { "vibration",     NAN,   NAN,   EVENT_SENSOR_ONLINE },
};

#define MOCK_READING_COUNT (sizeof(mock_readings) / sizeof(mock_readings[0]))

size_t display_get_reading_count(void)
{
    return MOCK_READING_COUNT;
}

bool display_get_reading(size_t index, const char **sensor_id,
                         float *value, float *value2, uint8_t *status)
{
    if (index >= MOCK_READING_COUNT) return false;
    const mock_reading_t *r = &mock_readings[index];
    if (sensor_id) *sensor_id = r->sensor_id;
    if (value) *value = r->value;
    if (value2) *value2 = r->value2;
    if (status) *status = r->status;
    return true;
}

// ---------------------------------------------------------------------------
// ESP log stub
// ---------------------------------------------------------------------------

void esp_log_write(int level, const char *tag, const char *fmt, ...)
{
    if (level <= 2) {
        const char *prefix = (level == 1) ? "E" : "W";
        fprintf(stderr, "[%s][%s] ", prefix, tag ? tag : "?");
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
    }
}

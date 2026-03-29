/**
 * @file mock_esp.c
 * @brief Mock implementations of ESP-IDF functions for desktop emulator
 *
 * Returns fake sensor data, secrets, WiFi info, and system stats
 * so display_screens.c renders realistic content without real hardware.
 */

#include "mock_esp.h"
#include "esp_netif.h"
#include <stdarg.h>
#include <stdio.h>
#include <time.h>
#include <stdlib.h>

// --------------------------------------------------------------------------
// Mock sensor data
// --------------------------------------------------------------------------

static sensor_reading_t mock_readings[] = {
    {
        .sensor_id = "temp_humidity",
        .timestamp_ms = 12345678,
        .value = 73.8f,
        .value2 = 55.2f,
        .status = SENSOR_STATUS_ONLINE,
    },
    {
        .sensor_id = "vibration",
        .timestamp_ms = 12345678,
        .value = NAN,      // Digital sensor — no float value
        .value2 = NAN,
        .status = SENSOR_STATUS_ONLINE,
    },
};

size_t sensor_manager_get_sensor_count(void) {
    return 2;
}

esp_err_t sensor_manager_get_reading_by_index(size_t index, sensor_reading_t *out) {
    if (index >= 2 || !out) return ESP_FAIL;
    *out = mock_readings[index];
    return ESP_OK;
}

const char *sensor_manager_get_sensor_id(size_t index) {
    if (index >= 2) return NULL;
    return mock_readings[index].sensor_id;
}

// --------------------------------------------------------------------------
// Mock secrets (intentionally fake — not real device secrets)
// --------------------------------------------------------------------------

static secrets_t mock_secrets = {
    .node_name = "boorker-MOCK",
    .web_password = "mock-web-pass",
    .ap_password = "mock-ap-pass",
    .ble_pop = "000000",
    .node_suffix = "MOCK",
};

const secrets_t *secrets_get(void) {
    return &mock_secrets;
}

// --------------------------------------------------------------------------
// Mock WiFi
// --------------------------------------------------------------------------

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) {
    if (!ap) return ESP_FAIL;
    strncpy((char *)ap->ssid, "MockNetwork", sizeof(ap->ssid));
    ap->rssi = -42;
    return ESP_OK;
}

esp_err_t esp_wifi_get_mac(wifi_interface_t ifx, uint8_t mac[6]) {
    (void)ifx;
    mac[0] = 0xDE; mac[1] = 0xAD; mac[2] = 0xBE;
    mac[3] = 0xEF; mac[4] = 0x00; mac[5] = 0x01;
    return ESP_OK;
}

// Mock netif — return non-NULL so IP code path is exercised
static int mock_netif_handle;

esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) {
    (void)key;
    return (esp_netif_t *)&mock_netif_handle;
}

// --------------------------------------------------------------------------
// Mock system info
// --------------------------------------------------------------------------

void esp_chip_info(esp_chip_info_t *info) {
    if (info) info->revision = 2;
}

uint32_t esp_get_free_heap_size(void) {
    return 161892;
}

int64_t esp_timer_get_time(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
        return 0;
    }
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

// --------------------------------------------------------------------------
// ESP log — forward errors and warnings to stderr
// --------------------------------------------------------------------------

void esp_log_write(int level, const char *tag, const char *fmt, ...) {
    // ESP log levels: 1=ERROR, 2=WARN, 3=INFO, 4=DEBUG, 5=VERBOSE
    if (level <= 2) {
        const char *prefix = (level == 1) ? "E" : "W";
        fprintf(stderr, "[%s][%s] ", prefix, tag ? tag : "?");
        va_list args;
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
    }
}

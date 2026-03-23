/**
 * @file mock_esp.c
 * @brief Mock implementations of ESP-IDF functions for desktop emulator
 *
 * Returns fake sensor data, credentials, WiFi info, and system stats
 * so display_screens.c renders realistic content without real hardware.
 */

#include "mock_esp.h"
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
// Mock credentials
// --------------------------------------------------------------------------

static credentials_t mock_creds = {
    .node_name = "boorker-E7E0",
    .web_password = "pT84xa3WXaBe",
    .ap_password = "sFq#36jcXphN",
    .ble_pop = "844365",
    .node_suffix = "E7E0",
};

const credentials_t *credentials_get(void) {
    return &mock_creds;
}

// --------------------------------------------------------------------------
// Mock WiFi
// --------------------------------------------------------------------------

esp_err_t esp_wifi_sta_get_ap_info(wifi_ap_record_t *ap) {
    if (!ap) return ESP_FAIL;
    strncpy((char *)ap->ssid, "Gnome", sizeof(ap->ssid));
    ap->rssi = -22;
    return ESP_OK;
}

esp_err_t esp_wifi_get_mac(wifi_interface_t ifx, uint8_t mac[6]) {
    (void)ifx;
    mac[0] = 0x3C; mac[1] = 0x0F; mac[2] = 0x02;
    mac[3] = 0xE7; mac[4] = 0xE7; mac[5] = 0xE0;
    return ESP_OK;
}

// Stub for esp_netif — emulator just shows the mock WiFi data
typedef struct { uint32_t ip; } esp_netif_ip_info_t;
typedef void* esp_netif_t;

esp_netif_t *esp_netif_get_handle_from_ifkey(const char *key) {
    (void)key;
    return NULL;  // Network screen will skip IP line
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
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

// --------------------------------------------------------------------------
// ESP log stub (just printf)
// --------------------------------------------------------------------------

void esp_log_write(int level, const char *tag, const char *fmt, ...) {
    (void)level; (void)tag; (void)fmt;
    // Silent in emulator
}

/**
 * @file mock_esp.h
 * @brief Mock ESP-IDF types and functions for desktop OLED emulator
 *
 * Provides stub implementations so display_screens.c compiles
 * natively on Linux without the ESP-IDF toolchain.
 */

#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

// ESP error type
typedef int esp_err_t;
#define ESP_OK 0
#define ESP_FAIL -1

// Sensor types (mirrors firmware/components/sensor_manager/include/sensor_types.h)
typedef enum {
    SENSOR_STATUS_ONLINE,
    SENSOR_STATUS_OFFLINE,
    SENSOR_STATUS_DISABLED,
    SENSOR_STATUS_ERROR,
} sensor_status_t;

typedef struct {
    const char *sensor_id;
    uint32_t timestamp_ms;
    float value;
    float value2;
    sensor_status_t status;
} sensor_reading_t;

static inline const char *sensor_status_name(sensor_status_t status) {
    switch (status) {
        case SENSOR_STATUS_ONLINE:  return "ONLINE";
        case SENSOR_STATUS_OFFLINE: return "OFFLINE";
        case SENSOR_STATUS_DISABLED: return "DISABLED";
        case SENSOR_STATUS_ERROR:   return "ERROR";
        default: return "UNKNOWN";
    }
}

// Credentials (mirrors firmware/components/credentials/include/credentials.h)
#define CRED_NODE_NAME_LEN  32
#define CRED_WEB_PASSWORD_LEN 33
#define CRED_AP_PASSWORD_LEN  33
#define CRED_BLE_POP_LEN      7
#define CRED_NODE_SUFFIX_LEN  5

typedef struct {
    char node_name[CRED_NODE_NAME_LEN];
    char web_password[CRED_WEB_PASSWORD_LEN];
    char ap_password[CRED_AP_PASSWORD_LEN];
    char ble_pop[CRED_BLE_POP_LEN];
    char node_suffix[CRED_NODE_SUFFIX_LEN];
} credentials_t;

// WiFi types
typedef struct {
    uint8_t ssid[33];
    int8_t rssi;
} wifi_ap_record_t;

typedef enum { WIFI_IF_STA = 0 } wifi_interface_t;

// Chip info
typedef struct {
    int revision;
} esp_chip_info_t;

// Version
#define BOORKER_VERSION_STRING "0.10.0-emu"

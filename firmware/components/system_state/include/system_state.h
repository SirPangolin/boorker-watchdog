#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Transport structs
// ---------------------------------------------------------------------------

typedef struct {
    bool connected;
    char ssid[33];
    int8_t rssi;
    char ip[16];
    uint8_t mac[6];
} system_wifi_t;

typedef struct {
    bool connected;
    uint32_t frequency_mhz;
    int8_t tx_power_dbm;
    uint8_t peer_count;
} system_lora_t;

// ---------------------------------------------------------------------------
// OTA
// ---------------------------------------------------------------------------

typedef struct {
    bool available;
    char version[32];
    uint32_t size_bytes;
} system_ota_update_t;

typedef struct {
    uint8_t state;
    uint32_t progress_bytes;
    uint32_t total_bytes;
    system_ota_update_t update;
} system_ota_t;

enum {
    SYSTEM_OTA_IDLE = 0,
    SYSTEM_OTA_DOWNLOADING,
    SYSTEM_OTA_VERIFYING,
    SYSTEM_OTA_PENDING_REBOOT,
};

// ---------------------------------------------------------------------------
// Reboot
// ---------------------------------------------------------------------------

typedef struct {
    bool pending;
    uint32_t remaining_seconds;
} system_reboot_t;

// ---------------------------------------------------------------------------
// Sensors
// ---------------------------------------------------------------------------

typedef struct {
    uint8_t configured;
    uint8_t online;
    uint8_t offline;
    uint8_t error;
} system_sensors_t;

// ---------------------------------------------------------------------------
// Peripherals
// ---------------------------------------------------------------------------

typedef struct {
    bool enabled;
    bool on;
    uint8_t brightness;
} system_led_t;

typedef struct {
    bool enabled;
    bool alerts_only;
    uint8_t volume;
} system_buzzer_t;

typedef struct {
    bool enabled;
    bool on;
    uint8_t contrast;
} system_display_t;

typedef struct {
    uint8_t count;
} system_buttons_t;

// ---------------------------------------------------------------------------
// Full system state
// ---------------------------------------------------------------------------

typedef struct {
    // Identity
    char node_name[32];
    char node_suffix[5];

    // Lifecycle
    bool claimed;

    // OTA
    system_ota_t ota;

    // Reboot
    system_reboot_t reboot;

    // Transports
    system_wifi_t wifi;
    system_lora_t lora;

    // Sensors
    system_sensors_t sensors;

    // Peripherals
    system_led_t led;
    system_buzzer_t buzzer;
    system_display_t display;
    system_buttons_t buttons;

    // System
    char firmware_version[16];
    char idf_version[32];
    uint8_t chip_revision_major;
    uint8_t chip_revision_minor;
    uint8_t chip_cores;
    uint32_t heap_free;
    uint32_t heap_total;
    uint32_t psram_free;
    uint32_t psram_total;
    int64_t uptime_us;
} system_state_t;

// ---------------------------------------------------------------------------
// Section change notifications
// ---------------------------------------------------------------------------

typedef enum {
    SYSTEM_STATE_IDENTITY_UPDATED = 0,
    SYSTEM_STATE_LIFECYCLE_UPDATED,
    SYSTEM_STATE_OTA_UPDATED,
    SYSTEM_STATE_REBOOT_UPDATED,
    SYSTEM_STATE_WIFI_UPDATED,
    SYSTEM_STATE_LORA_UPDATED,
    SYSTEM_STATE_SENSORS_UPDATED,
    SYSTEM_STATE_PERIPHERALS_UPDATED,
    SYSTEM_STATE_SYSTEM_UPDATED,
    SYSTEM_STATE_SECTION_MAX,
} system_state_section_t;

// ---------------------------------------------------------------------------
// API
// ---------------------------------------------------------------------------

esp_err_t system_state_init(void);
esp_err_t system_state_deinit(void);

/** Direct pointer to state (fast, no copy, possible torn reads on dual-core). */
const system_state_t *system_state_get(void);

/** Thread-safe copy of the full state (slower, no torn reads). */
esp_err_t system_state_copy(system_state_t *out);

/** Convenience — most common query. */
bool system_state_is_claimed(void);

// Section setters — each fires event_bus_notify with section tag
esp_err_t system_state_set_identity(const char *node_name, const char *node_suffix);
esp_err_t system_state_set_claimed(bool claimed);
esp_err_t system_state_set_ota(const system_ota_t *ota);
esp_err_t system_state_set_ota_state(uint8_t state);
esp_err_t system_state_set_reboot(bool pending, uint32_t remaining_seconds);
esp_err_t system_state_set_wifi(const system_wifi_t *wifi);
esp_err_t system_state_set_lora(const system_lora_t *lora);
esp_err_t system_state_set_sensors(const system_sensors_t *sensors);
esp_err_t system_state_set_led(const system_led_t *led);
esp_err_t system_state_set_buzzer(const system_buzzer_t *buzzer);
esp_err_t system_state_set_display(const system_display_t *display);
esp_err_t system_state_set_buttons(uint8_t count);
static inline const char *system_ota_state_name(uint8_t state) {
    static const char *names[] = { "idle", "downloading", "verifying", "pending_reboot" };
    return state < 4 ? names[state] : "unknown";
}

esp_err_t system_state_set_system(const char *firmware_version, const char *idf_version,
                                   uint8_t chip_major, uint8_t chip_minor, uint8_t chip_cores,
                                   uint32_t heap_free, uint32_t heap_total,
                                   uint32_t psram_free, uint32_t psram_total,
                                   int64_t uptime_us);

#ifdef __cplusplus
}
#endif

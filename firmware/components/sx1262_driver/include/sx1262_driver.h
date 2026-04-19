/**
 * @file sx1262_driver.h
 * @brief SX1262 LoRa radio HAL driver (handle-based, SPI)
 *
 * Pure hardware abstraction — no region enforcement, no mesh opinions.
 * See lora_manager for business logic and regional compliance.
 */
#pragma once

#include "esp_err.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ---------------------------------------------------------------------------
// Types
// ---------------------------------------------------------------------------

typedef struct sx1262_inst *sx1262_handle_t;

typedef struct {
    spi_host_device_t spi_host;
    gpio_num_t pin_nss;
    gpio_num_t pin_sck;
    gpio_num_t pin_mosi;
    gpio_num_t pin_miso;
    gpio_num_t pin_rst;
    gpio_num_t pin_busy;
    gpio_num_t pin_dio1;
    uint32_t spi_clock_hz;
} sx1262_config_t;

typedef struct {
    uint8_t spreading_factor;     // 7-12
    uint8_t bandwidth;            // 0=125kHz, 1=250kHz, 2=500kHz
    uint8_t coding_rate;          // 5-8 (4/5 to 4/8)
    bool low_data_rate_optimize;  // Auto-set for SF11/SF12 + 125kHz
} sx1262_mod_params_t;

typedef struct {
    uint16_t preamble_length;     // Default 8
    bool implicit_header;         // false = explicit (includes length)
    uint8_t payload_length;       // For implicit header mode
    bool crc_on;                  // Default true
    bool invert_iq;               // Default false
} sx1262_pkt_params_t;

typedef struct {
    const uint8_t *data;
    size_t length;
    int16_t rssi;
    int8_t snr;
} sx1262_rx_packet_t;

/**
 * @brief RX packet callback.
 *
 * Called from the driver's IRQ handler task when a packet is received.
 * The packet data pointer is valid ONLY for the duration of this callback.
 * Do not store the pointer — copy the data if async processing is needed.
 * A future packet_pool component will provide safe async buffer ownership.
 */
typedef void (*sx1262_rx_cb_t)(const sx1262_rx_packet_t *packet, void *ctx);

typedef struct {
    bool initialized;
    bool receiving;
    uint32_t frequency_hz;
    int8_t tx_power_dbm;
} sx1262_status_t;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

esp_err_t sx1262_create(const sx1262_config_t *config, sx1262_handle_t *out);
esp_err_t sx1262_destroy(sx1262_handle_t handle);

// ---------------------------------------------------------------------------
// Configuration
// ---------------------------------------------------------------------------

esp_err_t sx1262_set_frequency(sx1262_handle_t handle, uint32_t freq_hz);
esp_err_t sx1262_set_tx_power(sx1262_handle_t handle, int8_t power_dbm);
esp_err_t sx1262_set_modulation(sx1262_handle_t handle, const sx1262_mod_params_t *params);
esp_err_t sx1262_set_packet_params(sx1262_handle_t handle, const sx1262_pkt_params_t *params);

// ---------------------------------------------------------------------------
// Operations
// ---------------------------------------------------------------------------

esp_err_t sx1262_transmit(sx1262_handle_t handle, const uint8_t *data, size_t len, uint32_t timeout_ms);
esp_err_t sx1262_start_receive(sx1262_handle_t handle, sx1262_rx_cb_t callback, void *ctx);
esp_err_t sx1262_stop_receive(sx1262_handle_t handle);
esp_err_t sx1262_sleep(sx1262_handle_t handle);
esp_err_t sx1262_standby(sx1262_handle_t handle);

// ---------------------------------------------------------------------------
// Status
// ---------------------------------------------------------------------------

esp_err_t sx1262_get_packet_status(sx1262_handle_t handle, int16_t *rssi, int8_t *snr);
esp_err_t sx1262_get_status(sx1262_handle_t handle, sx1262_status_t *out);

#ifdef __cplusplus
}
#endif

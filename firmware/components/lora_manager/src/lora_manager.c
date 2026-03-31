/**
 * @file lora_manager.c
 * @brief LoRa radio manager — region enforcement, airtime tracking, state publishing
 */

#include "lora_manager.h"
#include "lora_regions.h"
#include "sx1262_driver.h"
#include "event_bus.h"
#include "system_state.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include <string.h>
#include <math.h>

static const char *TAG = "lora_manager";

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

static struct {
    sx1262_handle_t radio;
    const lora_region_config_t *region;
    int8_t tx_power_dbm;
    uint8_t spreading_factor;
    uint8_t bandwidth;
    uint8_t coding_rate;
    uint16_t preamble_length;
    bool initialized;
    bool listening;
    bool antenna_verified;
    uint32_t airtime_used_ms;
    uint32_t airtime_window_start;
} s_lora;

// ---------------------------------------------------------------------------
// Region resolution from Kconfig
// ---------------------------------------------------------------------------

static int get_region_index(void)
{
#if defined(CONFIG_LORA_REGION_US_915)
    return 0;
#elif defined(CONFIG_LORA_REGION_EU_868)
    return 1;
#elif defined(CONFIG_LORA_REGION_EU_433)
    return 2;
#elif defined(CONFIG_LORA_REGION_AU_915)
    return 3;
#else
    return 0;
#endif
}

// ---------------------------------------------------------------------------
// Time-on-air calculation (Semtech AN1200.13)
// ---------------------------------------------------------------------------

static uint32_t calculate_time_on_air_ms(size_t payload_len)
{
    float sf = (float)s_lora.spreading_factor;
    float bw_khz;
    switch (s_lora.bandwidth) {
        case 1:  bw_khz = 250.0f; break;
        case 2:  bw_khz = 500.0f; break;
        default: bw_khz = 125.0f; break;
    }
    float cr = (float)s_lora.coding_rate;

    float t_sym = powf(2.0f, sf) / (bw_khz * 1000.0f);
    float t_preamble = (s_lora.preamble_length + 4.25f) * t_sym;

    bool de = (sf >= 11 && bw_khz <= 125.0f);
    float de_val = de ? 1.0f : 0.0f;
    float payload_symbols = 8.0f + fmaxf(
        ceilf((8.0f * payload_len - 4.0f * sf + 28.0f + 16.0f) /
              (4.0f * (sf - 2.0f * de_val))) * (cr),
        0.0f);

    float t_payload = payload_symbols * t_sym;
    return (uint32_t)((t_preamble + t_payload) * 1000.0f);
}

// ---------------------------------------------------------------------------
// Duty cycle / airtime
// ---------------------------------------------------------------------------

static uint8_t get_effective_duty_cycle_pct(void)
{
    uint8_t kconfig_pct = CONFIG_LORA_MANAGER_DUTY_CYCLE_PCT;
    uint8_t region_max = s_lora.region ? s_lora.region->duty_cycle_pct : 10;
    return (kconfig_pct < region_max) ? kconfig_pct : region_max;
}

// ---------------------------------------------------------------------------
// Airtime tracking (rolling 1-hour window)
// ---------------------------------------------------------------------------

static void reset_airtime_window_if_needed(void)
{
    uint32_t now = (uint32_t)(esp_timer_get_time() / 1000);
    uint32_t elapsed = now - s_lora.airtime_window_start;
    if (elapsed >= 3600000) {
        s_lora.airtime_used_ms = 0;
        s_lora.airtime_window_start = now;
    }
}

// ---------------------------------------------------------------------------
// RX callback from sx1262_driver
// ---------------------------------------------------------------------------

static void on_rx_packet(const sx1262_rx_packet_t *packet, void *ctx)
{
    (void)ctx;

    // Print as string if printable ASCII, otherwise hex
    bool printable = true;
    for (size_t i = 0; i < packet->length && printable; i++) {
        if (packet->data[i] < 0x20 || packet->data[i] > 0x7E) {
            printable = false;
        }
    }

    // TODO: production builds should log RX data at DEBUG level only
    if (printable && packet->length > 0) {
        ESP_LOGI(TAG, "[RX] %zu bytes | RSSI:%d SNR:%d | \"%.*s\"",
                 packet->length, packet->rssi, packet->snr,
                 (int)packet->length, (const char *)packet->data);
    } else {
        ESP_LOGI(TAG, "[RX] %zu bytes | RSSI:%d SNR:%d",
                 packet->length, packet->rssi, packet->snr);
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, packet->data, packet->length, ESP_LOG_INFO);
    }

    if (packet->rssi > -130) {
        s_lora.antenna_verified = true;
    }

    event_notify_t evt = {
        .type = EVENT_NOTIFY_LORA_RX,
        .lora_rx = {
            .data = packet->data,
            .length = packet->length,
            .rssi = packet->rssi,
            .snr = packet->snr,
        },
    };
    event_bus_notify(&evt);
}

// ---------------------------------------------------------------------------
// system_state publishing
// ---------------------------------------------------------------------------

static void publish_state(bool connected)
{
    system_lora_t lora = {
        .connected = connected,
        .frequency_mhz = s_lora.region ? (uint32_t)(s_lora.region->frequency_hz / 1000000) : 0,
        .tx_power_dbm = s_lora.tx_power_dbm,
        .peer_count = 0,
    };
    system_state_set_lora(&lora);
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

esp_err_t lora_manager_init(void)
{
    if (s_lora.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    int region_idx = get_region_index();
    if (region_idx < 0 || (size_t)region_idx >= LORA_REGION_COUNT) {
        ESP_LOGE(TAG, "Invalid region index: %d", region_idx);
        return ESP_ERR_INVALID_ARG;
    }
    s_lora.region = &lora_regions[region_idx];

    if (!s_lora.region->tested) {
        ESP_LOGW(TAG, "Region %s is implemented from regulatory specs but untested. Use at your own risk.",
                 s_lora.region->name);
    }

    uint8_t effective_duty = get_effective_duty_cycle_pct();
    ESP_LOGI(TAG, "Region: %s (%lu Hz, max %d dBm, %d%% duty cycle, enforced)",
             s_lora.region->name,
             (unsigned long)s_lora.region->frequency_hz,
             s_lora.region->max_power_dbm,
             effective_duty);
    if (s_lora.region->max_dwell_time_ms > 0) {
        ESP_LOGI(TAG, "Dwell time limit: %u ms", s_lora.region->max_dwell_time_ms);
    }

    // Create radio
    sx1262_config_t radio_cfg = {
        .spi_host = SPI2_HOST,
        .pin_nss  = CONFIG_SX1262_PIN_NSS,
        .pin_sck  = CONFIG_SX1262_PIN_SCK,
        .pin_mosi = CONFIG_SX1262_PIN_MOSI,
        .pin_miso = CONFIG_SX1262_PIN_MISO,
        .pin_rst  = CONFIG_SX1262_PIN_RST,
        .pin_busy = CONFIG_SX1262_PIN_BUSY,
        .pin_dio1 = CONFIG_SX1262_PIN_DIO1,
        .spi_clock_hz = CONFIG_SX1262_SPI_CLOCK_HZ,
    };
    esp_err_t err = sx1262_create(&radio_cfg, &s_lora.radio);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Radio init failed: %s", esp_err_to_name(err));
        return err;
    }

    // Apply config
    err = sx1262_set_frequency(s_lora.radio, s_lora.region->frequency_hz);
    if (err != ESP_OK) { ESP_LOGE(TAG, "set_frequency failed"); goto fail; }

    s_lora.tx_power_dbm = CONFIG_LORA_MANAGER_TX_POWER_DBM;
    if (s_lora.tx_power_dbm > s_lora.region->max_power_dbm) {
        ESP_LOGW(TAG, "TX power %d dBm exceeds %s default of %d dBm. "
                 "Ensure you have appropriate authorization.",
                 s_lora.tx_power_dbm, s_lora.region->name,
                 s_lora.region->max_power_dbm);
    }
    err = sx1262_set_tx_power(s_lora.radio, s_lora.tx_power_dbm);
    if (err != ESP_OK) { ESP_LOGE(TAG, "set_tx_power failed"); goto fail; }

    s_lora.spreading_factor = CONFIG_LORA_MANAGER_SPREADING_FACTOR;
    s_lora.bandwidth = CONFIG_LORA_MANAGER_BANDWIDTH;
    s_lora.coding_rate = CONFIG_LORA_MANAGER_CODING_RATE;
    s_lora.preamble_length = CONFIG_LORA_MANAGER_PREAMBLE_LENGTH;

    bool ldro = (s_lora.spreading_factor >= 11 && s_lora.bandwidth == 0);
    sx1262_mod_params_t mod = {
        .spreading_factor = s_lora.spreading_factor,
        .bandwidth = s_lora.bandwidth,
        .coding_rate = s_lora.coding_rate,
        .low_data_rate_optimize = ldro,
    };
    err = sx1262_set_modulation(s_lora.radio, &mod);
    if (err != ESP_OK) { ESP_LOGE(TAG, "set_modulation failed"); goto fail; }

    sx1262_pkt_params_t pkt = {
        .preamble_length = s_lora.preamble_length,
        .implicit_header = false,
        .payload_length = 255,
        .crc_on = true,
        .invert_iq = false,
    };
    err = sx1262_set_packet_params(s_lora.radio, &pkt);
    if (err != ESP_OK) { ESP_LOGE(TAG, "set_packet_params failed"); goto fail; }

    s_lora.airtime_window_start = (uint32_t)(esp_timer_get_time() / 1000);
    s_lora.initialized = true;

    publish_state(true);

    ESP_LOGI(TAG, "Initialized (SF%d BW%d CR4/%d %d dBm)",
             s_lora.spreading_factor, s_lora.bandwidth == 0 ? 125 :
             s_lora.bandwidth == 1 ? 250 : 500,
             s_lora.coding_rate, s_lora.tx_power_dbm);
    return ESP_OK;

fail:
    sx1262_destroy(s_lora.radio);
    s_lora.radio = NULL;
    return err;
}

esp_err_t lora_manager_deinit(void)
{
    if (!s_lora.initialized) return ESP_ERR_INVALID_STATE;

    s_lora.initialized = false;
    s_lora.listening = false;
    publish_state(false);

    esp_err_t err = sx1262_destroy(s_lora.radio);
    s_lora.radio = NULL;
    return err;
}

esp_err_t lora_manager_send(const uint8_t *data, size_t len)
{
    if (!s_lora.initialized) return ESP_ERR_INVALID_STATE;
    if (data == NULL || len == 0) return ESP_ERR_INVALID_ARG;
    if (len > 255) return ESP_ERR_INVALID_SIZE;

    reset_airtime_window_if_needed();

    if (!lora_manager_can_transmit()) {
        ESP_LOGW(TAG, "TX blocked — airtime budget exceeded (%lu/%lu ms)",
                 (unsigned long)s_lora.airtime_used_ms,
                 (unsigned long)lora_manager_get_airtime_budget_ms());
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t toa = calculate_time_on_air_ms(len);
    if (s_lora.region->max_dwell_time_ms > 0 && toa > s_lora.region->max_dwell_time_ms) {
        ESP_LOGW(TAG, "TX blocked — time-on-air %lu ms exceeds %s dwell time limit of %u ms",
                 (unsigned long)toa, s_lora.region->name, s_lora.region->max_dwell_time_ms);
        return ESP_ERR_INVALID_SIZE;
    }

    bool was_listening = s_lora.listening;
    if (was_listening) {
        esp_err_t stop_err = sx1262_stop_receive(s_lora.radio);
        if (stop_err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to stop RX before TX: %s", esp_err_to_name(stop_err));
            return stop_err;
        }
        s_lora.listening = false;
    }

    esp_err_t err = sx1262_transmit(s_lora.radio, data, len, toa + 5000);

    if (err == ESP_OK) {
        s_lora.airtime_used_ms += toa;
        ESP_LOGI(TAG, "TX %zu bytes (airtime: %lu ms)", len, (unsigned long)toa);
    } else {
        ESP_LOGE(TAG, "TX failed: %s", esp_err_to_name(err));
    }

    if (was_listening) {
        esp_err_t rx_err = sx1262_start_receive(s_lora.radio, on_rx_packet, NULL);
        if (rx_err == ESP_OK) {
            s_lora.listening = true;
        } else {
            ESP_LOGE(TAG, "Failed to re-arm RX after TX: %s", esp_err_to_name(rx_err));
        }
    }

    return err;
}

esp_err_t lora_manager_start_listen(void)
{
    if (!s_lora.initialized) return ESP_ERR_INVALID_STATE;
    if (s_lora.listening) return ESP_OK;

    esp_err_t err = sx1262_start_receive(s_lora.radio, on_rx_packet, NULL);
    if (err == ESP_OK) {
        s_lora.listening = true;
        ESP_LOGI(TAG, "RX mode @ %.3f MHz",
                 s_lora.region->frequency_hz / 1000000.0f);
    }
    return err;
}

esp_err_t lora_manager_stop_listen(void)
{
    if (!s_lora.initialized) return ESP_ERR_INVALID_STATE;
    if (!s_lora.listening) return ESP_OK;

    esp_err_t err = sx1262_stop_receive(s_lora.radio);
    if (err == ESP_OK) {
        s_lora.listening = false;
        ESP_LOGI(TAG, "RX stopped, standby");
    }
    return err;
}

bool lora_manager_can_transmit(void)
{
    if (!s_lora.initialized) return false;
    reset_airtime_window_if_needed();
    uint32_t budget = lora_manager_get_airtime_budget_ms();
    return s_lora.airtime_used_ms < budget;
}

uint32_t lora_manager_get_airtime_used_ms(void)
{
    reset_airtime_window_if_needed();
    return s_lora.airtime_used_ms;
}

uint32_t lora_manager_get_airtime_budget_ms(void)
{
    if (!s_lora.region) return 0;
    return (uint32_t)(3600UL * 1000UL * get_effective_duty_cycle_pct() / 100);
}

esp_err_t lora_manager_get_status(lora_status_t *out)
{
    if (out == NULL) return ESP_ERR_INVALID_ARG;
    if (!s_lora.initialized) return ESP_ERR_INVALID_STATE;

    reset_airtime_window_if_needed();

    out->region_name = s_lora.region->name;
    out->frequency_hz = s_lora.region->frequency_hz;
    out->tx_power_dbm = s_lora.tx_power_dbm;
    out->region_max_power_dbm = s_lora.region->max_power_dbm;
    out->duty_cycle_pct = get_effective_duty_cycle_pct();
    out->receiving = s_lora.listening;
    out->antenna_verified = s_lora.antenna_verified;
    out->airtime_used_ms = s_lora.airtime_used_ms;
    out->airtime_budget_ms = lora_manager_get_airtime_budget_ms();
    return ESP_OK;
}

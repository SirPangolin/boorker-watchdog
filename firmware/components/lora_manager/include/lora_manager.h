/**
 * @file lora_manager.h
 * @brief LoRa radio manager — region enforcement, airtime tracking, console UI
 *
 * Smart layer above sx1262_driver. Enforces regional power/duty-cycle
 * limits (advisory, not blocking), tracks airtime, publishes radio state
 * to system_state, and provides console commands for TX/RX testing.
 */
#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *region_name;
    uint32_t frequency_hz;
    int8_t tx_power_dbm;
    int8_t region_max_power_dbm;
    uint8_t duty_cycle_pct;
    bool receiving;
    bool antenna_verified;
    uint32_t airtime_used_ms;
    uint32_t airtime_budget_ms;
} lora_status_t;

esp_err_t lora_manager_init(void);
esp_err_t lora_manager_deinit(void);

esp_err_t lora_manager_send(const uint8_t *data, size_t len);
esp_err_t lora_manager_start_listen(void);
esp_err_t lora_manager_stop_listen(void);

bool      lora_manager_can_transmit(void);
uint32_t  lora_manager_get_airtime_used_ms(void);
uint32_t  lora_manager_get_airtime_budget_ms(void);

esp_err_t lora_manager_get_status(lora_status_t *out);

esp_err_t lora_manager_register_console(void);

#ifdef __cplusplus
}
#endif

/**
 * @file lora_regions.h
 * @brief LoRa regional configuration table
 *
 * Regional regulatory maximums. The lora_manager enforces duty cycle
 * (blocking) and caps TX power per region. The duty_cycle_pct field
 * is the legal maximum — the actual enforced limit is the minimum of
 * this value and the Kconfig default (10% for all regions).
 *
 * Only US_915 has been tested on hardware.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char *name;
    uint32_t frequency_hz;
    int8_t max_power_dbm;
    uint8_t duty_cycle_pct;      // Regional legal maximum (100 = unrestricted)
    uint16_t max_dwell_time_ms;  // 0 = no dwell time limit
    bool tested;
} lora_region_config_t;

static const lora_region_config_t lora_regions[] = {
    /* US_915 */ { "US_915",  915000000, 22, 100,   0, true  },
    /* EU_868 */ { "EU_868",  869525000, 14,  10,   0, false },
    /* EU_433 */ { "EU_433",  433875000, 10,  10,   0, false },
    /* AU_915 */ { "AU_915",  915000000, 22, 100, 400, false },
};

#define LORA_REGION_COUNT (sizeof(lora_regions) / sizeof(lora_regions[0]))

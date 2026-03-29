/**
 * @file lora_regions.h
 * @brief LoRa regional configuration table
 *
 * Regulatory defaults per region. These are advisory — the lora_manager
 * warns but does not hard-block operation beyond these defaults.
 * Only US_915 has been tested on hardware.
 */
#pragma once

#include <stdint.h>
#include <stdbool.h>

typedef struct {
    const char *name;
    uint32_t frequency_hz;
    int8_t max_power_dbm;
    uint8_t duty_cycle_pct;     // 100 = unrestricted
    bool tested;
} lora_region_config_t;

static const lora_region_config_t lora_regions[] = {
    /* US_915 */ { "US_915",  915000000, 22, 100, true  },
    /* EU_868 */ { "EU_868",  869525000, 14,  10, false },
    /* EU_433 */ { "EU_433",  433875000, 10,  10, false },
    /* AU_915 */ { "AU_915",  915000000, 22, 100, false },
};

#define LORA_REGION_COUNT (sizeof(lora_regions) / sizeof(lora_regions[0]))

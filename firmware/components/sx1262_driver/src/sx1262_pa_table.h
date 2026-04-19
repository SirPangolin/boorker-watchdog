/**
 * @file sx1262_pa_table.h
 * @brief SX1262 PA optimization types
 *
 * Maps TX power (-9 to +22 dBm) to optimal paDutyCycle, hpMax, and paVal.
 * Values from RadioLib (github.com/jgromes/RadioLib) — empirically tuned.
 * See ATTRIBUTIONS.md for license details.
 *
 * Table data defined in sx1262_driver.c.
 * Index = power_dbm + 9 (e.g., -9 dBm = index 0, +22 dBm = index 31)
 */
#pragma once

#include <stdint.h>

typedef struct {
    uint8_t pa_duty_cycle;
    uint8_t hp_max;
    int8_t  pa_val;
} sx1262_pa_entry_t;

#define SX1262_PA_TABLE_MIN_DBM  (-9)
#define SX1262_PA_TABLE_MAX_DBM  (22)
#define SX1262_PA_TABLE_SIZE     (32)

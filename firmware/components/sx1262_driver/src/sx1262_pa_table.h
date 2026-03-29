/**
 * @file sx1262_pa_table.h
 * @brief SX1262 PA optimization lookup table
 *
 * Maps TX power (-9 to +22 dBm) to optimal paDutyCycle, hpMax, and paVal.
 * Values from RadioLib (github.com/jgromes/RadioLib) — empirically tuned.
 * See ATTRIBUTIONS.md for license details.
 *
 * Index = power_dbm + 9 (e.g., -9 dBm = index 0, +22 dBm = index 31)
 */
#pragma once

#include <stdint.h>

typedef struct {
    uint8_t pa_duty_cycle;
    uint8_t hp_max;
    int8_t  pa_val;
} sx1262_pa_entry_t;

static const sx1262_pa_entry_t sx1262_pa_table[32] = {
    /* -9 dBm */ { .pa_duty_cycle = 0, .hp_max = 0, .pa_val = -9 },
    /* -8 dBm */ { .pa_duty_cycle = 0, .hp_max = 0, .pa_val = -8 },
    /* -7 dBm */ { .pa_duty_cycle = 0, .hp_max = 0, .pa_val = -7 },
    /* -6 dBm */ { .pa_duty_cycle = 0, .hp_max = 0, .pa_val = -6 },
    /* -5 dBm */ { .pa_duty_cycle = 0, .hp_max = 0, .pa_val = -5 },
    /* -4 dBm */ { .pa_duty_cycle = 0, .hp_max = 0, .pa_val = -4 },
    /* -3 dBm */ { .pa_duty_cycle = 0, .hp_max = 0, .pa_val = -3 },
    /* -2 dBm */ { .pa_duty_cycle = 1, .hp_max = 0, .pa_val = -2 },
    /* -1 dBm */ { .pa_duty_cycle = 1, .hp_max = 0, .pa_val = -1 },
    /*  0 dBm */ { .pa_duty_cycle = 1, .hp_max = 0, .pa_val =  2 },
    /*  1 dBm */ { .pa_duty_cycle = 1, .hp_max = 0, .pa_val =  3 },
    /*  2 dBm */ { .pa_duty_cycle = 1, .hp_max = 0, .pa_val =  4 },
    /*  3 dBm */ { .pa_duty_cycle = 1, .hp_max = 1, .pa_val =  5 },
    /*  4 dBm */ { .pa_duty_cycle = 1, .hp_max = 1, .pa_val =  6 },
    /*  5 dBm */ { .pa_duty_cycle = 1, .hp_max = 1, .pa_val =  7 },
    /*  6 dBm */ { .pa_duty_cycle = 1, .hp_max = 2, .pa_val =  8 },
    /*  7 dBm */ { .pa_duty_cycle = 1, .hp_max = 2, .pa_val =  9 },
    /*  8 dBm */ { .pa_duty_cycle = 2, .hp_max = 2, .pa_val = 10 },
    /*  9 dBm */ { .pa_duty_cycle = 2, .hp_max = 2, .pa_val = 11 },
    /* 10 dBm */ { .pa_duty_cycle = 2, .hp_max = 3, .pa_val = 13 },
    /* 11 dBm */ { .pa_duty_cycle = 2, .hp_max = 3, .pa_val = 14 },
    /* 12 dBm */ { .pa_duty_cycle = 3, .hp_max = 3, .pa_val = 15 },
    /* 13 dBm */ { .pa_duty_cycle = 3, .hp_max = 4, .pa_val = 16 },
    /* 14 dBm */ { .pa_duty_cycle = 3, .hp_max = 5, .pa_val = 17 },
    /* 15 dBm */ { .pa_duty_cycle = 3, .hp_max = 5, .pa_val = 18 },
    /* 16 dBm */ { .pa_duty_cycle = 3, .hp_max = 5, .pa_val = 19 },
    /* 17 dBm */ { .pa_duty_cycle = 4, .hp_max = 6, .pa_val = 20 },
    /* 18 dBm */ { .pa_duty_cycle = 4, .hp_max = 6, .pa_val = 20 },
    /* 19 dBm */ { .pa_duty_cycle = 4, .hp_max = 6, .pa_val = 21 },
    /* 20 dBm */ { .pa_duty_cycle = 4, .hp_max = 7, .pa_val = 22 },
    /* 21 dBm */ { .pa_duty_cycle = 4, .hp_max = 7, .pa_val = 22 },
    /* 22 dBm */ { .pa_duty_cycle = 4, .hp_max = 7, .pa_val = 22 },
};

#define SX1262_PA_TABLE_MIN_DBM  (-9)
#define SX1262_PA_TABLE_MAX_DBM  (22)
#define SX1262_PA_TABLE_SIZE     (32)

/**
 * @file buzzer_patterns.h
 * @brief Internal pattern definitions and player
 */

#pragma once

#include "status_buzzer.h"
#include "esp_err.h"
#include <stddef.h>
#include <stdbool.h>

typedef struct {
    uint16_t on_ms;
    uint16_t off_ms;
} buzzer_step_t;

void buzzer_pattern_get(buzzer_preset_t preset, const buzzer_step_t **steps, size_t *count, bool *loops);
esp_err_t buzzer_pattern_player_init(void);
esp_err_t buzzer_pattern_player_deinit(void);
esp_err_t buzzer_pattern_play(buzzer_preset_t preset, uint8_t volume);
esp_err_t buzzer_pattern_stop(void);
bool buzzer_pattern_is_playing(void);

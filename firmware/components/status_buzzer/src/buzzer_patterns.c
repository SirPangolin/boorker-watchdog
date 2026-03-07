/**
 * @file buzzer_patterns.c
 * @brief Pattern definitions and esp_timer-based player
 */

#include "buzzer_patterns.h"
#include "buzzer_driver.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

static const char *TAG = "buzzer_patterns";

/**
 * @brief Pattern definitions
 *
 * Each step: {on_ms, off_ms}
 * - on_ms: Duration buzzer sounds (ms)
 * - off_ms: Silence after sound (ms), 0 = no gap before next step
 * - UINT16_MAX for on_ms = indefinite until stop() called (no timer scheduled)
 */
static const buzzer_step_t PATTERN_CHIRP[] = {{50, 0}};
static const buzzer_step_t PATTERN_DOUBLE_BEEP[] = {{100, 100}, {100, 0}};
static const buzzer_step_t PATTERN_TRIPLE_BEEP[] = {{100, 100}, {100, 100}, {100, 0}};
static const buzzer_step_t PATTERN_ALARM[] = {{200, 200}};  // Loops
static const buzzer_step_t PATTERN_SOLID[] = {{UINT16_MAX, 0}};  // Indefinite until stop()

// Player state
static struct {
    SemaphoreHandle_t mutex;
    esp_timer_handle_t timer;
    const buzzer_step_t *steps;
    size_t step_count;
    size_t current_step;
    bool is_on_phase;
    bool loops;
    bool playing;
    uint8_t volume;
} s_player = {0};

void buzzer_pattern_get(buzzer_preset_t preset, const buzzer_step_t **steps, size_t *count, bool *loops)
{
    switch (preset) {
        case BUZZER_PRESET_CHIRP:
            *steps = PATTERN_CHIRP; *count = 1; *loops = false; break;
        case BUZZER_PRESET_DOUBLE_BEEP:
            *steps = PATTERN_DOUBLE_BEEP; *count = 2; *loops = false; break;
        case BUZZER_PRESET_TRIPLE_BEEP:
            *steps = PATTERN_TRIPLE_BEEP; *count = 3; *loops = false; break;
        case BUZZER_PRESET_ALARM:
            *steps = PATTERN_ALARM; *count = 1; *loops = true; break;
        case BUZZER_PRESET_SOLID:
            *steps = PATTERN_SOLID; *count = 1; *loops = false; break;
        default:
            *steps = NULL; *count = 0; *loops = false;
    }
}

static void timer_callback(void *arg);

/**
 * @brief Start the next step's on-phase
 *
 * Helper to reduce code duplication in advance_pattern().
 * Called with mutex held.
 */
static void start_next_on_phase(void)
{
    s_player.is_on_phase = true;
    esp_err_t err = buzzer_driver_set_volume(s_player.volume);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set volume: %s", esp_err_to_name(err));
    }
    err = buzzer_driver_on();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to turn buzzer on: %s", esp_err_to_name(err));
        s_player.playing = false;
        return;
    }

    uint16_t on_ms = s_player.steps[s_player.current_step].on_ms;
    // UINT16_MAX = indefinite (no timer), used by SOLID pattern
    if (on_ms < UINT16_MAX) {
        err = esp_timer_start_once(s_player.timer, on_ms * 1000);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "Failed to start timer: %s", esp_err_to_name(err));
            s_player.playing = false;
        }
    }
}

/**
 * @brief Move pattern forward to next phase/step
 *
 * Called with mutex held from timer callback.
 */
static void advance_pattern(void)
{
    esp_err_t err;

    if (s_player.is_on_phase) {
        // Finished on-phase, turn buzzer off
        err = buzzer_driver_off();
        if (err != ESP_OK) {
            // Critical: buzzer is still on! Reschedule to retry.
            ESP_LOGW(TAG, "Failed to turn buzzer off: %s - retrying", esp_err_to_name(err));
            esp_timer_start_once(s_player.timer, 1000);  // Retry in 1ms
            return;
        }

        uint16_t off_ms = s_player.steps[s_player.current_step].off_ms;
        if (off_ms > 0) {
            // Start off-phase timer
            s_player.is_on_phase = false;
            err = esp_timer_start_once(s_player.timer, off_ms * 1000);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Failed to start off timer: %s", esp_err_to_name(err));
                s_player.playing = false;
            }
        } else {
            // No gap - move to next step immediately
            s_player.current_step++;
            if (s_player.current_step >= s_player.step_count) {
                if (s_player.loops) {
                    s_player.current_step = 0;
                } else {
                    s_player.playing = false;
                    return;
                }
            }
            start_next_on_phase();
        }
    } else {
        // Finished off-phase, move to next step
        s_player.current_step++;
        if (s_player.current_step >= s_player.step_count) {
            if (s_player.loops) {
                s_player.current_step = 0;
            } else {
                s_player.playing = false;
                return;
            }
        }
        start_next_on_phase();
    }
}

static void timer_callback(void *arg)
{
    if (xSemaphoreTake(s_player.mutex, 0) == pdTRUE) {
        if (s_player.playing) {
            advance_pattern();
        }
        xSemaphoreGive(s_player.mutex);
    } else {
        // Mutex held by another context - reschedule to retry
        // This causes timing jitter but ensures pattern completes
        esp_timer_start_once(s_player.timer, 1000);  // Retry in 1ms
    }
}

esp_err_t buzzer_pattern_player_init(void)
{
    s_player.mutex = xSemaphoreCreateMutex();
    if (!s_player.mutex) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    esp_timer_create_args_t timer_args = {
        .callback = timer_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "buzzer_pattern"
    };
    esp_err_t err = esp_timer_create(&timer_args, &s_player.timer);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create timer: %s", esp_err_to_name(err));
        vSemaphoreDelete(s_player.mutex);
        return err;
    }

    ESP_LOGI(TAG, "Pattern player initialized");
    return ESP_OK;
}

esp_err_t buzzer_pattern_player_deinit(void)
{
    buzzer_pattern_stop();
    if (s_player.timer) {
        esp_timer_delete(s_player.timer);
        s_player.timer = NULL;
    }
    if (s_player.mutex) {
        vSemaphoreDelete(s_player.mutex);
        s_player.mutex = NULL;
    }
    return ESP_OK;
}

esp_err_t buzzer_pattern_play(buzzer_preset_t preset, uint8_t volume)
{
    if (!s_player.mutex) return ESP_ERR_INVALID_STATE;

    // Use finite timeout to avoid deadlock with timer callback
    if (xSemaphoreTake(s_player.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in play");
        return ESP_ERR_TIMEOUT;
    }

    // Stop any current pattern
    esp_err_t err = esp_timer_stop(s_player.timer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE is ok (timer not running)
        ESP_LOGW(TAG, "Failed to stop timer: %s", esp_err_to_name(err));
    }
    err = buzzer_driver_off();
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "Failed to turn off buzzer: %s", esp_err_to_name(err));
    }

    bool loops;
    buzzer_pattern_get(preset, &s_player.steps, &s_player.step_count, &loops);
    if (!s_player.steps || s_player.step_count == 0) {
        xSemaphoreGive(s_player.mutex);
        return ESP_ERR_INVALID_ARG;
    }

    s_player.loops = loops;
    s_player.current_step = 0;
    s_player.is_on_phase = true;
    s_player.playing = true;
    s_player.volume = volume;

    err = buzzer_driver_set_volume(volume);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to set volume: %s", esp_err_to_name(err));
    }

    err = buzzer_driver_on();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start buzzer: %s", esp_err_to_name(err));
        s_player.playing = false;
        xSemaphoreGive(s_player.mutex);
        return err;
    }

    uint16_t on_ms = s_player.steps[0].on_ms;
    // UINT16_MAX = indefinite (no timer needed)
    if (on_ms < UINT16_MAX) {
        err = esp_timer_start_once(s_player.timer, on_ms * 1000);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to start timer: %s", esp_err_to_name(err));
            buzzer_driver_off();
            s_player.playing = false;
            xSemaphoreGive(s_player.mutex);
            return err;
        }
    }

    xSemaphoreGive(s_player.mutex);
    ESP_LOGD(TAG, "Playing %s at %d%%", buzzer_preset_name(preset), volume);
    return ESP_OK;
}

esp_err_t buzzer_pattern_stop(void)
{
    if (!s_player.mutex) return ESP_ERR_INVALID_STATE;

    // Use finite timeout to avoid deadlock
    if (xSemaphoreTake(s_player.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in stop");
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;

    esp_err_t err = esp_timer_stop(s_player.timer);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        // ESP_ERR_INVALID_STATE is ok (timer not running)
        ESP_LOGW(TAG, "Failed to stop timer: %s", esp_err_to_name(err));
        ret = err;
    }

    err = buzzer_driver_off();
    if (err != ESP_OK && err != ESP_ERR_TIMEOUT) {
        ESP_LOGW(TAG, "Failed to turn off buzzer: %s", esp_err_to_name(err));
        if (ret == ESP_OK) ret = err;
    }

    s_player.playing = false;
    xSemaphoreGive(s_player.mutex);

    ESP_LOGD(TAG, "Stopped");
    return ret;
}

bool buzzer_pattern_is_playing(void)
{
    // Reading a bool is atomic on ESP32 - returns point-in-time snapshot
    // Caller should not make critical decisions based on this value
    // as it may change immediately after return
    return s_player.playing;
}

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

// Pattern definitions
static const buzzer_step_t PATTERN_CHIRP[] = {{50, 0}};
static const buzzer_step_t PATTERN_DOUBLE_BEEP[] = {{100, 100}, {100, 0}};
static const buzzer_step_t PATTERN_TRIPLE_BEEP[] = {{100, 100}, {100, 100}, {100, 0}};
static const buzzer_step_t PATTERN_ALARM[] = {{200, 200}};  // Loops
static const buzzer_step_t PATTERN_SOLID[] = {{UINT16_MAX, 0}};  // Until stopped

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

static void advance_pattern(void)
{
    if (s_player.is_on_phase) {
        buzzer_driver_off();
        uint16_t off_ms = s_player.steps[s_player.current_step].off_ms;
        if (off_ms > 0) {
            s_player.is_on_phase = false;
            esp_timer_start_once(s_player.timer, off_ms * 1000);
        } else {
            s_player.current_step++;
            if (s_player.current_step >= s_player.step_count) {
                if (s_player.loops) {
                    s_player.current_step = 0;
                } else {
                    s_player.playing = false;
                    return;
                }
            }
            s_player.is_on_phase = true;
            buzzer_driver_set_volume(s_player.volume);
            buzzer_driver_on();
            uint16_t on_ms = s_player.steps[s_player.current_step].on_ms;
            if (on_ms < UINT16_MAX) {
                esp_timer_start_once(s_player.timer, on_ms * 1000);
            }
        }
    } else {
        s_player.current_step++;
        if (s_player.current_step >= s_player.step_count) {
            if (s_player.loops) {
                s_player.current_step = 0;
            } else {
                s_player.playing = false;
                return;
            }
        }
        s_player.is_on_phase = true;
        buzzer_driver_set_volume(s_player.volume);
        buzzer_driver_on();
        uint16_t on_ms = s_player.steps[s_player.current_step].on_ms;
        if (on_ms < UINT16_MAX) {
            esp_timer_start_once(s_player.timer, on_ms * 1000);
        }
    }
}

static void timer_callback(void *arg)
{
    if (xSemaphoreTake(s_player.mutex, 0) == pdTRUE) {
        if (s_player.playing) {
            advance_pattern();
        }
        xSemaphoreGive(s_player.mutex);
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

    xSemaphoreTake(s_player.mutex, portMAX_DELAY);

    esp_timer_stop(s_player.timer);
    buzzer_driver_off();

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

    buzzer_driver_set_volume(volume);
    buzzer_driver_on();
    uint16_t on_ms = s_player.steps[0].on_ms;
    if (on_ms < UINT16_MAX) {
        esp_timer_start_once(s_player.timer, on_ms * 1000);
    }

    xSemaphoreGive(s_player.mutex);
    ESP_LOGD(TAG, "Playing %s at %d%%", buzzer_preset_name(preset), volume);
    return ESP_OK;
}

esp_err_t buzzer_pattern_stop(void)
{
    if (!s_player.mutex) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_player.mutex, portMAX_DELAY);
    esp_timer_stop(s_player.timer);
    buzzer_driver_off();
    s_player.playing = false;
    xSemaphoreGive(s_player.mutex);

    ESP_LOGD(TAG, "Stopped");
    return ESP_OK;
}

bool buzzer_pattern_is_playing(void)
{
    return s_player.playing;
}

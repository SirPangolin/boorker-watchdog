/**
 * @file button_driver.c
 * @brief Generic N-button GPIO driver with press detection
 *
 * Uses a periodic polling timer (traditional embedded approach) instead of
 * edge-triggered ISR. A 10ms tick reads GPIO state, debounces via
 * consecutive stable readings, and tracks hold duration for short/long/
 * very-long press detection.
 *
 * This avoids all ISR timing races and GPIO noise issues that plague
 * edge-triggered debounce implementations.
 */

#include "button_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include <string.h>

static const char *TAG = "button_driver";

/** Polling interval in microseconds (10ms) */
#define POLL_INTERVAL_US  10000

/** Number of consecutive stable readings for debounce (N * 10ms) */
#define DEBOUNCE_TICKS(ms) ((ms) / (POLL_INTERVAL_US / 1000))

/**
 * @brief Internal state for a single registered button
 */
typedef struct {
    bool active;
    button_config_t config;
    bool debounced_pressed;      /**< Debounced press state */
    bool raw_last;               /**< Last raw GPIO reading */
    uint8_t stable_count;        /**< Consecutive stable readings */
    uint8_t debounce_threshold;  /**< Readings needed for debounce */
    int64_t press_start_us;      /**< When press was first debounced */
    bool long_fired;
    bool very_long_fired;
} button_slot_t;

/**
 * @brief Driver context
 */
typedef struct {
    SemaphoreHandle_t mutex;
    bool initialized;
    esp_timer_handle_t poll_timer;
    button_slot_t slots[CONFIG_BUTTON_DRIVER_MAX_BUTTONS];
} button_driver_ctx_t;

static button_driver_ctx_t s_ctx = {0};

/**
 * @brief Apply Kconfig defaults for zero-valued config fields
 */
static void apply_defaults(button_config_t *cfg)
{
    if (cfg->debounce_ms == 0) {
        cfg->debounce_ms = CONFIG_BUTTON_DRIVER_DEBOUNCE_MS;
    }
    if (cfg->long_press_ms == 0) {
        cfg->long_press_ms = CONFIG_BUTTON_DRIVER_LONG_PRESS_MS;
    }
    if (cfg->very_long_press_ms == 0) {
        cfg->very_long_press_ms = CONFIG_BUTTON_DRIVER_VERY_LONG_PRESS_MS;
    }

    // Validate: very_long must be greater than long (unless disabled)
    if (cfg->very_long_press_ms != BUTTON_VERY_LONG_DISABLED &&
        cfg->very_long_press_ms <= cfg->long_press_ms) {
        ESP_LOGW(TAG, "very_long_press_ms (%u) must be > long_press_ms (%u), adjusting",
                 cfg->very_long_press_ms, cfg->long_press_ms);
        cfg->very_long_press_ms = cfg->long_press_ms + 2000;
    }
}

/**
 * @brief Read GPIO accounting for active_low
 */
static inline bool read_pressed(const button_slot_t *slot)
{
    int level = gpio_get_level(slot->config.gpio);
    return slot->config.active_low ? (level == 0) : (level == 1);
}

/**
 * @brief Poll timer callback — runs every 10ms, processes all buttons
 *
 * Called from esp_timer task context (high priority). Keep fast.
 */
static void poll_timer_callback(void *arg)
{
    (void)arg;

    for (int i = 0; i < CONFIG_BUTTON_DRIVER_MAX_BUTTONS; i++) {
        button_slot_t *slot = &s_ctx.slots[i];
        if (!slot->active) continue;

        bool raw = read_pressed(slot);

        // Debounce: count consecutive stable readings
        if (raw == slot->raw_last) {
            if (slot->stable_count < 255) {
                slot->stable_count++;
            }
        } else {
            slot->stable_count = 0;
            slot->raw_last = raw;
            continue;  // State changing, wait for it to settle
        }

        // Not yet debounced
        if (slot->stable_count < slot->debounce_threshold) {
            continue;
        }

        // State debounced — process exactly once on transition
        if (raw != slot->debounced_pressed) {
            slot->debounced_pressed = raw;

            if (slot->config.mode == BUTTON_MODE_LATCHED) {
                button_press_t event = raw ? BUTTON_LATCH_ON : BUTTON_LATCH_OFF;
                slot->config.callback(i, event, slot->config.ctx);
                continue;
            }

            // Momentary: press or release
            if (raw) {
                // Press
                slot->press_start_us = esp_timer_get_time();
                slot->long_fired = false;
                slot->very_long_fired = false;
            } else {
                // Release — fire SHORT only if long/very-long weren't fired
                if (!slot->long_fired && !slot->very_long_fired) {
                    slot->config.callback(i, BUTTON_PRESS_SHORT, slot->config.ctx);
                }
            }
            continue;
        }

        // Held state — check for long/very-long thresholds (momentary only)
        if (slot->config.mode == BUTTON_MODE_MOMENTARY && slot->debounced_pressed) {
            int64_t held_ms = (esp_timer_get_time() - slot->press_start_us) / 1000;

            if (!slot->very_long_fired &&
                slot->config.very_long_press_ms != BUTTON_VERY_LONG_DISABLED &&
                held_ms >= slot->config.very_long_press_ms) {
                slot->very_long_fired = true;
                slot->config.callback(i, BUTTON_PRESS_VERY_LONG, slot->config.ctx);

            } else if (!slot->long_fired && held_ms >= slot->config.long_press_ms) {
                slot->long_fired = true;
                slot->config.callback(i, BUTTON_PRESS_LONG, slot->config.ctx);
            }
        }
    }
}

// -------------------------------------------------------------------------
// Public API
// -------------------------------------------------------------------------

esp_err_t button_driver_init(void)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing button driver (max %d buttons, %dms poll)",
             CONFIG_BUTTON_DRIVER_MAX_BUTTONS, POLL_INTERVAL_US / 1000);

    s_ctx.mutex = xSemaphoreCreateMutex();
    if (s_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Clear all slots
    memset(s_ctx.slots, 0, sizeof(s_ctx.slots));

    // Create periodic poll timer
    esp_timer_create_args_t timer_args = {
        .callback = poll_timer_callback,
        .name = "btn_poll",
    };
    esp_err_t ret = esp_timer_create(&timer_args, &s_ctx.poll_timer);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create poll timer: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return ret;
    }

    ret = esp_timer_start_periodic(s_ctx.poll_timer, POLL_INTERVAL_US);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start poll timer: %s", esp_err_to_name(ret));
        esp_timer_delete(s_ctx.poll_timer);
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return ret;
    }

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "Button driver initialized");
    return ESP_OK;
}

esp_err_t button_driver_deinit(void)
{
    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing button driver");

    esp_err_t stop_ret = esp_timer_stop(s_ctx.poll_timer);
    if (stop_ret != ESP_OK && stop_ret != ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "Timer stop failed: %s", esp_err_to_name(stop_ret));
    }
    esp_err_t del_ret = esp_timer_delete(s_ctx.poll_timer);
    if (del_ret != ESP_OK) {
        ESP_LOGE(TAG, "Timer delete failed: %s", esp_err_to_name(del_ret));
        return del_ret;
    }
    s_ctx.poll_timer = NULL;

    memset(s_ctx.slots, 0, sizeof(s_ctx.slots));

    s_ctx.initialized = false;

    vSemaphoreDelete(s_ctx.mutex);
    s_ctx.mutex = NULL;

    ESP_LOGI(TAG, "Button driver deinitialized");
    return ESP_OK;
}

int button_driver_register(const button_config_t *config)
{
    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return -1;
    }

    if (config == NULL || config->callback == NULL) {
        ESP_LOGE(TAG, "Invalid config or missing callback");
        return -1;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    // Find empty slot
    int slot_id = -1;
    for (int i = 0; i < CONFIG_BUTTON_DRIVER_MAX_BUTTONS; i++) {
        if (!s_ctx.slots[i].active) {
            slot_id = i;
            break;
        }
    }

    if (slot_id < 0) {
        ESP_LOGE(TAG, "No available button slots (max %d)", CONFIG_BUTTON_DRIVER_MAX_BUTTONS);
        xSemaphoreGive(s_ctx.mutex);
        return -1;
    }

    button_slot_t *slot = &s_ctx.slots[slot_id];
    memset(slot, 0, sizeof(*slot));
    slot->config = *config;
    apply_defaults(&slot->config);

    // Configure GPIO as input
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << config->gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = config->active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = config->active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_DISABLE,  // No ISR — polling only
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d config failed: %s", config->gpio, esp_err_to_name(ret));
        xSemaphoreGive(s_ctx.mutex);
        return -1;
    }

    // Initialize state
    slot->raw_last = read_pressed(slot);
    slot->debounced_pressed = slot->raw_last;
    slot->stable_count = 255;  // Already stable
    slot->debounce_threshold = DEBOUNCE_TICKS(slot->config.debounce_ms);
    if (slot->debounce_threshold < 2) slot->debounce_threshold = 2;

    slot->active = true;

    ESP_LOGI(TAG, "Registered button %d on GPIO %d (%s, %s, debounce %dms)",
             slot_id, config->gpio,
             config->active_low ? "active LOW" : "active HIGH",
             config->mode == BUTTON_MODE_MOMENTARY ? "momentary" : "latched",
             slot->config.debounce_ms);

    xSemaphoreGive(s_ctx.mutex);
    return slot_id;
}

esp_err_t button_driver_unregister(int button_id)
{
    if (!s_ctx.initialized) {
        ESP_LOGW(TAG, "Not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (button_id < 0 || button_id >= CONFIG_BUTTON_DRIVER_MAX_BUTTONS) {
        ESP_LOGE(TAG, "Invalid button_id %d", button_id);
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    button_slot_t *slot = &s_ctx.slots[button_id];
    if (!slot->active) {
        ESP_LOGW(TAG, "Button %d not registered", button_id);
        xSemaphoreGive(s_ctx.mutex);
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Unregistered button %d (GPIO %d)", button_id, slot->config.gpio);
    slot->active = false;

    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

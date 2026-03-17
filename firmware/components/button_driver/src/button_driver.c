/**
 * @file button_driver.c
 * @brief Generic N-button GPIO driver with press detection
 *
 * Uses GPIO ISR for edge detection + FreeRTOS software timer for
 * debounce and press duration measurement. Supports momentary
 * (short/long/very-long) and latched (toggle) button modes.
 */

#include "button_driver.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/timers.h"
#include "sdkconfig.h"

static const char *TAG = "button_driver";

/**
 * @brief Internal state for a single registered button
 */
typedef struct {
    bool active;                     /**< Slot in use */
    button_config_t config;          /**< Registration config (copy) */
    TimerHandle_t timer;             /**< Debounce + press detection timer */
    int64_t press_start_us;          /**< Timestamp of press start (esp_timer_get_time) */
    bool pressed;                    /**< Current debounced press state */
    bool long_fired;                 /**< Long press callback already fired */
    bool very_long_fired;            /**< Very long press callback already fired */
    bool last_latched_state;         /**< Last stable state for latched mode */
} button_slot_t;

/**
 * @brief Driver context
 */
typedef struct {
    SemaphoreHandle_t mutex;
    bool initialized;
    bool isr_installed;
    button_slot_t slots[CONFIG_BUTTON_DRIVER_MAX_BUTTONS];
} button_driver_ctx_t;

static button_driver_ctx_t s_ctx = {0};

// Forward declarations
static void IRAM_ATTR gpio_isr_handler(void *arg);
static void button_timer_callback(TimerHandle_t xTimer);

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
}

/**
 * @brief Read debounced GPIO level accounting for active_low
 * @return true if button is in "pressed" state
 */
static inline bool read_pressed(const button_slot_t *slot)
{
    int level = gpio_get_level(slot->config.gpio);
    return slot->config.active_low ? (level == 0) : (level == 1);
}

// -------------------------------------------------------------------------
// ISR Handler
// -------------------------------------------------------------------------

static void IRAM_ATTR gpio_isr_handler(void *arg)
{
    button_slot_t *slot = (button_slot_t *)arg;
    if (!slot->active) {
        return;
    }
    // Restart debounce timer from ISR
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xTimerResetFromISR(slot->timer, &xHigherPriorityTaskWoken);
    if (xHigherPriorityTaskWoken) {
        portYIELD_FROM_ISR();
    }
}

// -------------------------------------------------------------------------
// Timer Callback (runs in FreeRTOS timer daemon task)
// -------------------------------------------------------------------------

static void button_timer_callback(TimerHandle_t xTimer)
{
    button_slot_t *slot = (button_slot_t *)pvTimerGetTimerID(xTimer);
    if (!slot || !slot->active) {
        return;
    }

    bool is_pressed = read_pressed(slot);
    int button_id = (int)(slot - s_ctx.slots);

    if (slot->config.mode == BUTTON_MODE_LATCHED) {
        // Latched mode: report state changes
        if (is_pressed != slot->last_latched_state) {
            slot->last_latched_state = is_pressed;
            button_press_t event = is_pressed ? BUTTON_LATCH_ON : BUTTON_LATCH_OFF;
            slot->config.callback(button_id, event, slot->config.ctx);
        }
        return;
    }

    // Momentary mode
    if (is_pressed && !slot->pressed) {
        // Press detected (after debounce)
        slot->pressed = true;
        slot->press_start_us = esp_timer_get_time();
        slot->long_fired = false;
        slot->very_long_fired = false;
        // Schedule timer to check for long press
        xTimerChangePeriod(slot->timer,
            pdMS_TO_TICKS(slot->config.long_press_ms - slot->config.debounce_ms),
            0);
        xTimerStart(slot->timer, 0);

    } else if (is_pressed && slot->pressed) {
        // Still held — check for long/very-long thresholds
        int64_t held_ms = (esp_timer_get_time() - slot->press_start_us) / 1000;

        if (!slot->very_long_fired &&
            slot->config.very_long_press_ms != 0xFFFF &&
            held_ms >= slot->config.very_long_press_ms) {
            slot->very_long_fired = true;
            slot->config.callback(button_id, BUTTON_PRESS_VERY_LONG, slot->config.ctx);
            // No more timers needed — already at max threshold

        } else if (!slot->long_fired && held_ms >= slot->config.long_press_ms) {
            slot->long_fired = true;
            slot->config.callback(button_id, BUTTON_PRESS_LONG, slot->config.ctx);
            // Schedule timer for very-long check
            if (slot->config.very_long_press_ms != 0xFFFF) {
                uint16_t remaining = slot->config.very_long_press_ms - slot->config.long_press_ms;
                xTimerChangePeriod(slot->timer, pdMS_TO_TICKS(remaining), 0);
                xTimerStart(slot->timer, 0);
            }
        }

    } else if (!is_pressed && slot->pressed) {
        // Released
        slot->pressed = false;
        xTimerStop(slot->timer, 0);

        // Only fire SHORT if long/very-long weren't already fired
        if (!slot->long_fired && !slot->very_long_fired) {
            slot->config.callback(button_id, BUTTON_PRESS_SHORT, slot->config.ctx);
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

    ESP_LOGI(TAG, "Initializing button driver (max %d buttons)", CONFIG_BUTTON_DRIVER_MAX_BUTTONS);

    s_ctx.mutex = xSemaphoreCreateMutex();
    if (s_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Install GPIO ISR service (shared across all buttons)
    esp_err_t ret = gpio_install_isr_service(0);
    if (ret == ESP_ERR_INVALID_STATE) {
        // ISR service already installed by another component — that's fine
        ESP_LOGD(TAG, "GPIO ISR service already installed");
        s_ctx.isr_installed = false;  // We didn't install it, don't uninstall
    } else if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install GPIO ISR service: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return ret;
    } else {
        s_ctx.isr_installed = true;
    }

    // Clear all slots
    for (int i = 0; i < CONFIG_BUTTON_DRIVER_MAX_BUTTONS; i++) {
        s_ctx.slots[i].active = false;
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

    xSemaphoreTake(s_ctx.mutex, portMAX_DELAY);

    // Unregister all active buttons
    for (int i = 0; i < CONFIG_BUTTON_DRIVER_MAX_BUTTONS; i++) {
        if (s_ctx.slots[i].active) {
            gpio_isr_handler_remove(s_ctx.slots[i].config.gpio);
            if (s_ctx.slots[i].timer) {
                xTimerStop(s_ctx.slots[i].timer, portMAX_DELAY);
                xTimerDelete(s_ctx.slots[i].timer, portMAX_DELAY);
                s_ctx.slots[i].timer = NULL;
            }
            s_ctx.slots[i].active = false;
        }
    }

    if (s_ctx.isr_installed) {
        gpio_uninstall_isr_service();
        s_ctx.isr_installed = false;
    }

    s_ctx.initialized = false;

    xSemaphoreGive(s_ctx.mutex);
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
    slot->config = *config;  // Copy config
    apply_defaults(&slot->config);

    // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << config->gpio),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = config->active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = config->active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = GPIO_INTR_ANYEDGE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO %d config failed: %s", config->gpio, esp_err_to_name(ret));
        xSemaphoreGive(s_ctx.mutex);
        return -1;
    }

    // Create debounce/press timer
    char timer_name[16];
    snprintf(timer_name, sizeof(timer_name), "btn_%d", slot_id);
    slot->timer = xTimerCreate(timer_name,
        pdMS_TO_TICKS(slot->config.debounce_ms),
        pdFALSE,  // One-shot
        slot,      // Timer ID = slot pointer
        button_timer_callback);
    if (slot->timer == NULL) {
        ESP_LOGE(TAG, "Failed to create timer for button %d", slot_id);
        xSemaphoreGive(s_ctx.mutex);
        return -1;
    }

    // Initialize state
    slot->pressed = false;
    slot->long_fired = false;
    slot->very_long_fired = false;
    slot->press_start_us = 0;
    slot->last_latched_state = read_pressed(slot);

    // Install ISR handler
    ret = gpio_isr_handler_add(config->gpio, gpio_isr_handler, slot);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add ISR for GPIO %d: %s", config->gpio, esp_err_to_name(ret));
        xTimerDelete(slot->timer, portMAX_DELAY);
        slot->timer = NULL;
        xSemaphoreGive(s_ctx.mutex);
        return -1;
    }

    slot->active = true;

    ESP_LOGI(TAG, "Registered button %d on GPIO %d (%s, %s)",
             slot_id, config->gpio,
             config->active_low ? "active LOW" : "active HIGH",
             config->mode == BUTTON_MODE_MOMENTARY ? "momentary" : "latched");

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

    gpio_isr_handler_remove(slot->config.gpio);

    if (slot->timer) {
        xTimerStop(slot->timer, portMAX_DELAY);
        xTimerDelete(slot->timer, portMAX_DELAY);
        slot->timer = NULL;
    }

    ESP_LOGI(TAG, "Unregistered button %d (GPIO %d)", button_id, slot->config.gpio);
    slot->active = false;

    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

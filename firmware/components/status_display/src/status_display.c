/**
 * @file status_display.c
 * @brief Status display core — task, state machine, event_bus integration
 *
 * ANDON channel subscriber that renders system state on an SSD1306 OLED
 * via the u8g2 graphics library. Handles button input for navigation.
 */

#include "sdkconfig.h"

#if CONFIG_STATUS_DISPLAY_ENABLED

#include "status_display.h"
#include "display_hal.h"
#include "event_bus.h"
#include "credentials.h"
#include "system_state.h"
#include "u8g2.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "nvs_flash.h"
#include "nvs.h"

#include <string.h>
#include <stdio.h>

static const char *TAG = "status_display";

#include "display_internal.h"

// --------------------------------------------------------------------------
// Screen state machine
// --------------------------------------------------------------------------

typedef enum {
    SCREEN_SPLASH,
    SCREEN_FIRST_BOOT,
    SCREEN_DASHBOARD,
    SCREEN_DRILL_NETWORK,
    SCREEN_DRILL_LORA,
    SCREEN_DRILL_SYSTEM,
    SCREEN_DRILL_NODES,
    SCREEN_DRILL_SENSORS,
    SCREEN_ALERT,
    SCREEN_OFF,
} screen_state_t;

// Task notification bits
#define NOTIFY_EVENT_CHANGE  (1 << 0)
#define NOTIFY_BUTTON_SHORT  (1 << 1)
#define NOTIFY_BUTTON_LONG   (1 << 2)
#define NOTIFY_BUTTON_VLONG  (1 << 3)
#define NOTIFY_READING_UPDATE (1 << 4)

#define MAX_CACHED_READINGS CONFIG_STATUS_DISPLAY_MAX_SENSORS

typedef struct {
    char sensor_id[32];
    float value;
    float value2;
    uint8_t status;
} cached_reading_t;

// --------------------------------------------------------------------------
// Context
// --------------------------------------------------------------------------

static struct {
    TaskHandle_t task;
    bool initialized;

    u8g2_t u8g2;

    // Screen state
    screen_state_t screen;
    screen_state_t pre_alert_screen;
    bool display_on;
    bool alert_silenced;

    // Event bus state
    event_state_t event_state;

    // Sensor reading cache (populated via event_bus notify)
    cached_reading_t readings[MAX_CACHED_READINGS];
    size_t reading_count;
    bool sensors_ready;

    // Rotation
    int rotate_page;
    int rotate_total;
    int64_t last_interaction_us;

} s_ctx = {
    .initialized = false,
    .display_on = true,
    .screen = SCREEN_SPLASH,
    .event_state = EVENT_OFF,
};

// --------------------------------------------------------------------------
// Reading cache accessors (used by display_screens.c)
// --------------------------------------------------------------------------

size_t display_get_reading_count(void)
{
    return s_ctx.reading_count;
}

bool display_get_reading(size_t index, const char **sensor_id,
                         float *value, float *value2, uint8_t *status)
{
    if (index >= s_ctx.reading_count) return false;
    const cached_reading_t *r = &s_ctx.readings[index];
    if (sensor_id) *sensor_id = r->sensor_id;
    if (value) *value = r->value;
    if (value2) *value2 = r->value2;
    if (status) *status = r->status;
    return true;
}

// --------------------------------------------------------------------------
// Event bus notify callback
// --------------------------------------------------------------------------

static void cache_sensor_reading(const event_notify_t *event)
{
    const char *id = event->sensor_reading.sensor_id;

    // Update existing or append
    for (size_t i = 0; i < s_ctx.reading_count; i++) {
        if (strncmp(s_ctx.readings[i].sensor_id, id, sizeof(s_ctx.readings[i].sensor_id)) == 0) {
            s_ctx.readings[i].value = event->sensor_reading.value;
            s_ctx.readings[i].value2 = event->sensor_reading.value2;
            s_ctx.readings[i].status = event->sensor_reading.status;
            return;
        }
    }

    if (s_ctx.reading_count < MAX_CACHED_READINGS) {
        cached_reading_t *r = &s_ctx.readings[s_ctx.reading_count++];
        strncpy(r->sensor_id, id, sizeof(r->sensor_id) - 1);
        r->value = event->sensor_reading.value;
        r->value2 = event->sensor_reading.value2;
        r->status = event->sensor_reading.status;
    }
}

static void on_notify(const event_notify_t *event, void *ctx)
{
    (void)ctx;
    if (!s_ctx.task) return;

    switch (event->type) {
    case EVENT_NOTIFY_BUTTON: {
        uint32_t bits = 0;
        switch (event->button.press) {
        case EVENT_PRESS_SHORT:     bits = NOTIFY_BUTTON_SHORT; break;
        case EVENT_PRESS_LONG:      bits = NOTIFY_BUTTON_LONG;  break;
        case EVENT_PRESS_VERY_LONG: bits = NOTIFY_BUTTON_VLONG; break;
        default: return;
        }
        xTaskNotify(s_ctx.task, bits, eSetBits);
        break;
    }
    case EVENT_NOTIFY_SENSOR_READING:
        cache_sensor_reading(event);
        xTaskNotify(s_ctx.task, NOTIFY_READING_UPDATE, eSetBits);
        break;
    case EVENT_NOTIFY_SENSORS_READY:
        s_ctx.sensors_ready = true;
        xTaskNotify(s_ctx.task, NOTIFY_READING_UPDATE, eSetBits);
        break;
    default:
        break;
    }
}

// --------------------------------------------------------------------------
// Event bus callback (fires from event_bus context — must not block)
// --------------------------------------------------------------------------

static void on_event_state_change(event_state_t state, void *ctx)
{
    (void)ctx;
    s_ctx.event_state = state;

    if (s_ctx.task) {
        xTaskNotify(s_ctx.task, NOTIFY_EVENT_CHANGE, eSetBits);
    }
}

// --------------------------------------------------------------------------
// State machine logic
// --------------------------------------------------------------------------

static void handle_event_change(void)
{
    event_state_t state = s_ctx.event_state;

    // Alert override — takes over any screen
    if (state == EVENT_ALERT_CRITICAL || state == EVENT_ALERT_ACTIVE || state == EVENT_ERROR) {
        if (s_ctx.screen != SCREEN_ALERT) {
            // Don't return to OFF after alert — user should see the dashboard
            s_ctx.pre_alert_screen = (s_ctx.screen == SCREEN_OFF) ? SCREEN_DASHBOARD : s_ctx.screen;
            s_ctx.screen = SCREEN_ALERT;
            s_ctx.alert_silenced = false;
        }
        return;
    }

    // If alert was showing and cleared, return to previous screen
    if (s_ctx.screen == SCREEN_ALERT) {
        s_ctx.screen = s_ctx.pre_alert_screen;
        s_ctx.alert_silenced = false;
    }

    // First boot screen
    if (state == EVENT_FIRST_BOOT && !system_state_is_claimed()) {
        s_ctx.screen = SCREEN_FIRST_BOOT;
        return;
    }

    // If on first boot and device just got claimed, transition to dashboard
    if (s_ctx.screen == SCREEN_FIRST_BOOT && system_state_is_claimed()) {
        s_ctx.screen = SCREEN_DASHBOARD;
        s_ctx.rotate_page = 0;
        return;
    }

    // Default: dashboard (if not already on a drill-down)
    if (s_ctx.screen == SCREEN_SPLASH || s_ctx.screen == SCREEN_FIRST_BOOT) {
        s_ctx.screen = SCREEN_DASHBOARD;
        s_ctx.rotate_page = 0;
    }
}

static void handle_button_short(void)
{
    s_ctx.last_interaction_us = esp_timer_get_time();

    switch (s_ctx.screen) {
    case SCREEN_OFF:
        // Wake display
        s_ctx.display_on = true;
        u8g2_SetPowerSave(&s_ctx.u8g2, 0);
        s_ctx.screen = SCREEN_DASHBOARD;
        s_ctx.rotate_page = 0;
        ESP_LOGI(TAG, "Display woken by button press");
        break;

    case SCREEN_ALERT:
        if (!s_ctx.alert_silenced) {
            // First press: silence buzzer
            s_ctx.alert_silenced = true;
            ESP_LOGI(TAG, "Alert silenced");
        } else {
            // Second press: acknowledge alert
            ESP_LOGI(TAG, "Alert acknowledged");
            s_ctx.screen = SCREEN_DASHBOARD;
            s_ctx.rotate_page = 0;
            s_ctx.alert_silenced = false;
        }
        break;

    case SCREEN_DASHBOARD:
        // Enter drill-down
        s_ctx.screen = SCREEN_DRILL_NETWORK;
        break;

    case SCREEN_DRILL_NETWORK:
        s_ctx.screen = SCREEN_DRILL_LORA;
        break;
    case SCREEN_DRILL_LORA:
        s_ctx.screen = SCREEN_DRILL_SYSTEM;
        break;
    case SCREEN_DRILL_SYSTEM:
        s_ctx.screen = SCREEN_DRILL_NODES;
        break;
    case SCREEN_DRILL_NODES:
        s_ctx.screen = SCREEN_DRILL_SENSORS;
        break;
    case SCREEN_DRILL_SENSORS:
        // Wrap back to dashboard
        s_ctx.screen = SCREEN_DASHBOARD;
        s_ctx.rotate_page = 0;
        break;

    default:
        break;
    }
}

static void handle_button_long(void)
{
    if (s_ctx.screen == SCREEN_OFF) return;

    // Turn display off
    s_ctx.display_on = false;
    s_ctx.screen = SCREEN_OFF;
    u8g2_SetPowerSave(&s_ctx.u8g2, 1);
    ESP_LOGI(TAG, "Display off (long press)");
}

static void handle_button_very_long(void)
{
    ESP_LOGW(TAG, "Rebooting (very long press)...");

    // Wake display if off so user sees the reboot message
    u8g2_SetPowerSave(&s_ctx.u8g2, 0);

    u8g2_ClearBuffer(&s_ctx.u8g2);
    u8g2_SetFont(&s_ctx.u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(&s_ctx.u8g2, 20, 36, "REBOOTING...");
    u8g2_SendBuffer(&s_ctx.u8g2);

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
}

// --------------------------------------------------------------------------
// Render
// --------------------------------------------------------------------------

static void render_current_screen(void)
{
    if (!s_ctx.display_on) return;

    u8g2_ClearBuffer(&s_ctx.u8g2);

    switch (s_ctx.screen) {
    case SCREEN_SPLASH:
        // Handled in splash loop, not here
        break;

    case SCREEN_FIRST_BOOT: {
        const credentials_t *creds = credentials_get();
        // TODO: get actual IP when available
        screen_first_boot(&s_ctx.u8g2, creds, "--");
        break;
    }

    case SCREEN_DASHBOARD: {
        int total_metrics = screen_get_metric_count();
        s_ctx.rotate_total = total_metrics > 0 ? total_metrics : 1;
        screen_dashboard_card(&s_ctx.u8g2, s_ctx.rotate_page);
        break;
    }

    case SCREEN_ALERT: {
        const char *msg = "ALERT ACTIVE";
        if (s_ctx.event_state == EVENT_ALERT_CRITICAL || s_ctx.event_state == EVENT_ERROR) {
            msg = "CRITICAL!";
        }
        screen_alert(&s_ctx.u8g2, "LOCAL", msg, s_ctx.alert_silenced);
        break;
    }

    case SCREEN_DRILL_NETWORK:
        screen_network(&s_ctx.u8g2);
        break;
    case SCREEN_DRILL_LORA:
        screen_lora(&s_ctx.u8g2);
        break;
    case SCREEN_DRILL_SYSTEM:
        screen_system(&s_ctx.u8g2);
        break;
    case SCREEN_DRILL_NODES:
        screen_nodes(&s_ctx.u8g2);
        break;
    case SCREEN_DRILL_SENSORS:
        screen_sensors(&s_ctx.u8g2);
        break;

    default:
        break;
    }

    u8g2_SendBuffer(&s_ctx.u8g2);
}

// --------------------------------------------------------------------------
// Display task
// --------------------------------------------------------------------------

static void display_task(void *arg)
{
    (void)arg;

    // Register with event_bus so readings cache during splash
    esp_err_t ret = event_bus_register_channel_ex("display", on_event_state_change, on_notify, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register event bus channel: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Registered as event bus channel");
    }

    // Splash: show Millie logo, wait for system ready
    // Break when: min branding time met AND (readings arrived OR sensors_ready OR timeout)
    int min_frames = CONFIG_STATUS_DISPLAY_SPLASH_MIN_MS / CONFIG_STATUS_DISPLAY_THROBBER_MS;
    int max_frames = CONFIG_STATUS_DISPLAY_SPLASH_TIMEOUT_MS / CONFIG_STATUS_DISPLAY_THROBBER_MS;
    for (int i = 0; i < max_frames; i++) {
        bool min_met = (i >= min_frames);
        bool data_ready = (s_ctx.reading_count > 0 || s_ctx.sensors_ready);
        bool first_boot = (s_ctx.screen == SCREEN_FIRST_BOOT);
        if (min_met && (data_ready || first_boot)) break;

        u8g2_ClearBuffer(&s_ctx.u8g2);
        screen_splash(&s_ctx.u8g2, i % 3);
        u8g2_SendBuffer(&s_ctx.u8g2);
        vTaskDelay(pdMS_TO_TICKS(CONFIG_STATUS_DISPLAY_THROBBER_MS));
    }

    // Transition based on what arrived during splash
    if (s_ctx.screen != SCREEN_FIRST_BOOT) {
        s_ctx.screen = SCREEN_DASHBOARD;
        s_ctx.rotate_page = 0;
    }

    s_ctx.last_interaction_us = esp_timer_get_time();

    // Phase 3: Main render loop
    while (true) {
        // Calculate wait time based on current screen
        TickType_t wait_ticks;
        if (s_ctx.screen == SCREEN_DASHBOARD) {
            wait_ticks = pdMS_TO_TICKS(CONFIG_STATUS_DISPLAY_ROTATE_INTERVAL_MS);
        } else if (s_ctx.screen == SCREEN_OFF) {
            wait_ticks = portMAX_DELAY;
        } else {
            wait_ticks = pdMS_TO_TICKS(1000);  // 1s refresh for drill-down/alert
        }

        uint32_t notify_value = 0;
        xTaskNotifyWait(0, UINT32_MAX, &notify_value, wait_ticks);

        // Process notifications
        if (notify_value & NOTIFY_BUTTON_VLONG) {
            handle_button_very_long();  // Does not return (reboots)
        }
        if (notify_value & NOTIFY_BUTTON_LONG) {
            handle_button_long();
        }
        if (notify_value & NOTIFY_BUTTON_SHORT) {
            handle_button_short();
        }
        if (notify_value & NOTIFY_EVENT_CHANGE) {
            handle_event_change();
        }

        // Auto-rotate dashboard
        if (s_ctx.screen == SCREEN_DASHBOARD && notify_value == 0) {
            s_ctx.rotate_page = (s_ctx.rotate_page + 1) % (s_ctx.rotate_total > 0 ? s_ctx.rotate_total : 1);
        }

        // Auto-return from drill-down
        if (s_ctx.screen >= SCREEN_DRILL_NETWORK && s_ctx.screen <= SCREEN_DRILL_SENSORS) {
            int64_t idle_ms = (esp_timer_get_time() - s_ctx.last_interaction_us) / 1000;
            if (idle_ms > CONFIG_STATUS_DISPLAY_DRILL_TIMEOUT_MS) {
                s_ctx.screen = SCREEN_DASHBOARD;
                s_ctx.rotate_page = 0;
                ESP_LOGD(TAG, "Auto-return to dashboard");
            }
        }

        render_current_screen();
    }
}

// --------------------------------------------------------------------------
// Public API
// --------------------------------------------------------------------------

esp_err_t status_display_init(void)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Initializing status display");

    // Initialize I2C HAL for u8g2
    esp_err_t ret = display_hal_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Display HAL init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Setup u8g2 for SSD1306 128x64 I2C (full framebuffer mode)
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(&s_ctx.u8g2, U8G2_R0,
        display_hal_i2c_byte_cb, display_hal_gpio_delay_cb);

    // Set I2C address (left-shifted: 0x3C << 1 = 0x78)
    u8x8_SetI2CAddress(&s_ctx.u8g2.u8x8, 0x78);

    // Initialize display hardware
    u8g2_InitDisplay(&s_ctx.u8g2);
    u8g2_SetPowerSave(&s_ctx.u8g2, 0);  // Turn on
    u8g2_SetContrast(&s_ctx.u8g2, CONFIG_STATUS_DISPLAY_CONTRAST);

    // Clear display
    u8g2_ClearBuffer(&s_ctx.u8g2);
    u8g2_SendBuffer(&s_ctx.u8g2);

    ESP_LOGI(TAG, "u8g2 SSD1306 128x64 initialized");

    // Create display task
    BaseType_t task_ret = xTaskCreate(display_task, "display",
        CONFIG_STATUS_DISPLAY_TASK_STACK, NULL,
        CONFIG_STATUS_DISPLAY_TASK_PRIORITY, &s_ctx.task);
    if (task_ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create display task");
        display_hal_deinit();
        return ESP_ERR_NO_MEM;
    }

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "Status display initialized (task started)");

    return ESP_OK;
}

esp_err_t status_display_deinit(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGI(TAG, "Deinitializing status display");

    // Stop task
    if (s_ctx.task) {
        vTaskDelete(s_ctx.task);
        s_ctx.task = NULL;
    }

    // Turn off display
    u8g2_SetPowerSave(&s_ctx.u8g2, 1);

    // Deinit HAL
    display_hal_deinit();

    // Cleanup
    s_ctx.initialized = false;

    ESP_LOGI(TAG, "Status display deinitialized");
    return ESP_OK;
}

#endif /* CONFIG_STATUS_DISPLAY_ENABLED */

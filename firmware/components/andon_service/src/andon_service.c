/**
 * @file andon_service.c
 * @brief ANDON notification hub service implementation
 *
 * Implements a centralized notification hub for system and business states.
 * Uses bitmask state tracking with priority resolution and channel callbacks.
 */

#include "andon_service.h"
#include "device_state.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include <string.h>
#include <inttypes.h>

static const char *TAG = "andon_service";

// Mutex timeout
#define MUTEX_TIMEOUT_MS        100
#define DEINIT_MUTEX_TIMEOUT_MS 500

// State names for logging
static const char *state_names[] = {
    [ANDON_FIRST_BOOT]           = "FIRST_BOOT",
    [ANDON_ERROR]                = "ERROR",
    [ANDON_WIFI_PROVISIONING]    = "WIFI_PROVISIONING",
    [ANDON_WIFI_RECONNECTING]    = "WIFI_RECONNECTING",
    [ANDON_WIFI_CONNECTING]      = "WIFI_CONNECTING",
    [ANDON_TAILSCALE_CONNECTING] = "TAILSCALE_CONNECTING",
    [ANDON_CONNECTED]            = "CONNECTED",
    [ANDON_OFF]                  = "OFF",
    [ANDON_ALERT_CRITICAL]       = "ALERT_CRITICAL",
    [ANDON_ALERT_ACTIVE]         = "ALERT_ACTIVE",
    [ANDON_SENSOR_WARNING]       = "SENSOR_WARNING",
};

// Verify state_names array matches enum
_Static_assert(sizeof(state_names) / sizeof(state_names[0]) == ANDON_MAX,
               "state_names array size must match ANDON_MAX");

// Channel registration structure
typedef struct {
    const char *name;
    andon_channel_cb_t cb;
    void *ctx;
} andon_channel_t;

// Static context structure
static struct {
    bool initialized;
    uint32_t active_states;              // Bitmask of active states
    andon_state_t current_active;        // Cached highest priority active state
    andon_channel_t channels[CONFIG_ANDON_MAX_CHANNELS];
    size_t channel_count;
    SemaphoreHandle_t mutex;
} s_ctx = {
    .initialized = false,
    .active_states = 0,
    .current_active = ANDON_OFF,
    .channel_count = 0,
    .mutex = NULL,
};

// --------------------------------------------------------------------------
// Helper Functions
// --------------------------------------------------------------------------

/**
 * @brief Get highest priority active state (lowest bit set)
 * @return Highest priority active state, or ANDON_OFF if none active
 */
static andon_state_t get_highest_priority_state(void)
{
    if (s_ctx.active_states == 0) {
        return ANDON_OFF;
    }

    // Find lowest set bit (highest priority state)
    for (int i = 0; i < ANDON_MAX; i++) {
        if (s_ctx.active_states & (1U << i)) {
            return (andon_state_t)i;
        }
    }

    // Should be unreachable if active_states != 0
    ESP_LOGE(TAG, "BUG: active_states=0x%08" PRIx32 " but no bit found in range 0-%d",
             s_ctx.active_states, ANDON_MAX - 1);
    return ANDON_OFF;
}

/**
 * @brief Notify all registered channels of state change
 *
 * Called with mutex held. Callbacks are invoked synchronously.
 *
 * @param new_state The new active state to notify channels about
 */
static void notify_channels(andon_state_t new_state)
{
    for (size_t i = 0; i < s_ctx.channel_count; i++) {
        if (s_ctx.channels[i].cb != NULL) {
            ESP_LOGD(TAG, "Notifying channel '%s' of state %s",
                     s_ctx.channels[i].name, state_names[new_state]);
            s_ctx.channels[i].cb(new_state, s_ctx.channels[i].ctx);
        }
    }
}

/**
 * @brief Update active state and notify channels if changed
 *
 * Must be called with mutex held.
 */
static void update_and_notify(void)
{
    andon_state_t new_active = get_highest_priority_state();

    if (new_active != s_ctx.current_active) {
        ESP_LOGI(TAG, "State: %s -> %s",
                 state_names[s_ctx.current_active], state_names[new_active]);
        s_ctx.current_active = new_active;
        notify_channels(new_active);
    }
}

// --------------------------------------------------------------------------
// Core API Implementation
// --------------------------------------------------------------------------

esp_err_t andon_init(void)
{
    if (s_ctx.initialized) {
        ESP_LOGD(TAG, "Already initialized");
        return ESP_OK;
    }

    // Create mutex
    s_ctx.mutex = xSemaphoreCreateMutex();
    if (s_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Initialize state
    s_ctx.active_states = 0;
    s_ctx.current_active = ANDON_OFF;
    s_ctx.channel_count = 0;
    memset(s_ctx.channels, 0, sizeof(s_ctx.channels));

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "Initialized (max channels: %d)", CONFIG_ANDON_MAX_CHANNELS);

    return ESP_OK;
}

esp_err_t andon_deinit(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Take mutex before cleanup with longer timeout
    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(DEINIT_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Mutex timeout during deinit");
        return ESP_ERR_TIMEOUT;
    }

    s_ctx.initialized = false;
    s_ctx.active_states = 0;
    s_ctx.current_active = ANDON_OFF;
    s_ctx.channel_count = 0;
    memset(s_ctx.channels, 0, sizeof(s_ctx.channels));

    // Give back mutex before deleting
    xSemaphoreGive(s_ctx.mutex);
    vSemaphoreDelete(s_ctx.mutex);
    s_ctx.mutex = NULL;

    ESP_LOGI(TAG, "Deinitialized");
    return ESP_OK;
}

esp_err_t andon_set_state(andon_state_t state)
{
    if (state >= ANDON_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Check business state gate before acquiring mutex
    if (andon_is_business_state(state) && !device_state_is_claimed()) {
        ESP_LOGD(TAG, "Business state %s blocked (device unclaimed)", state_names[state]);
        return ESP_ERR_NOT_ALLOWED;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in set_state");
        return ESP_ERR_TIMEOUT;
    }

    // Set bit in active states bitmask
    uint32_t old_states = s_ctx.active_states;
    s_ctx.active_states |= (1U << state);

    if (s_ctx.active_states != old_states) {
        ESP_LOGI(TAG, "State + %s (active: 0x%08lx)",
                 state_names[state], (unsigned long)s_ctx.active_states);
        update_and_notify();
    }

    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

esp_err_t andon_clear_state(andon_state_t state)
{
    if (state >= ANDON_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in clear_state");
        return ESP_ERR_TIMEOUT;
    }

    // Clear bit in active states bitmask
    uint32_t old_states = s_ctx.active_states;
    s_ctx.active_states &= ~(1U << state);

    if (s_ctx.active_states != old_states) {
        ESP_LOGI(TAG, "State - %s (active: 0x%08lx)",
                 state_names[state], (unsigned long)s_ctx.active_states);
        update_and_notify();
    }

    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

andon_state_t andon_get_active_state(void)
{
    if (!s_ctx.initialized) {
        return ANDON_OFF;
    }

    // Reading a single enum is atomic on ESP32
    return s_ctx.current_active;
}

esp_err_t andon_register_channel(const char *name, andon_channel_cb_t cb, void *ctx)
{
    if (name == NULL || cb == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in register_channel");
        return ESP_ERR_TIMEOUT;
    }

    // Check for available slot
    if (s_ctx.channel_count >= CONFIG_ANDON_MAX_CHANNELS) {
        ESP_LOGE(TAG, "Max channels (%d) reached, cannot register '%s'",
                 CONFIG_ANDON_MAX_CHANNELS, name);
        xSemaphoreGive(s_ctx.mutex);
        return ESP_ERR_NO_MEM;
    }

    // Register channel
    size_t idx = s_ctx.channel_count;
    s_ctx.channels[idx].name = name;
    s_ctx.channels[idx].cb = cb;
    s_ctx.channels[idx].ctx = ctx;
    s_ctx.channel_count++;

    ESP_LOGI(TAG, "Registered channel '%s' (%zu/%d)",
             name, s_ctx.channel_count, CONFIG_ANDON_MAX_CHANNELS);

    // Notify new channel of current state immediately
    andon_state_t current = s_ctx.current_active;
    xSemaphoreGive(s_ctx.mutex);

    // Call callback outside mutex to avoid deadlock
    ESP_LOGD(TAG, "Initial notify to '%s': %s", name, state_names[current]);
    cb(current, ctx);

    return ESP_OK;
}

// --------------------------------------------------------------------------
// Utility Functions
// --------------------------------------------------------------------------

bool andon_is_business_state(andon_state_t state)
{
    return (state == ANDON_ALERT_CRITICAL ||
            state == ANDON_ALERT_ACTIVE ||
            state == ANDON_SENSOR_WARNING);
}

const char *andon_state_name(andon_state_t state)
{
    if (state >= ANDON_MAX) {
        return "UNKNOWN";
    }
    return state_names[state];
}

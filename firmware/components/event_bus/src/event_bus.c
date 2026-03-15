/**
 * @file event_bus.c
 * @brief Event bus notification hub implementation
 *
 * Implements a centralized pub/sub notification hub for system and business states.
 * Uses bitmask state tracking with priority resolution and channel callbacks.
 */

#include "event_bus.h"
#include "system_state.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

#include <string.h>
#include <inttypes.h>

static const char *TAG = "event_bus";

// Mutex timeout
#define MUTEX_TIMEOUT_MS        100
#define DEINIT_MUTEX_TIMEOUT_MS 500

// State names for logging
static const char *state_names[] = {
    [EVENT_FIRST_BOOT]           = "FIRST_BOOT",
    [EVENT_ERROR]                = "ERROR",
    [EVENT_WIFI_PROVISIONING]    = "WIFI_PROVISIONING",
    [EVENT_WIFI_RECONNECTING]    = "WIFI_RECONNECTING",
    [EVENT_WIFI_CONNECTING]      = "WIFI_CONNECTING",
    [EVENT_TAILSCALE_CONNECTING] = "TAILSCALE_CONNECTING",
    [EVENT_CONNECTED]            = "CONNECTED",
    [EVENT_OFF]                  = "OFF",
    [EVENT_ALERT_CRITICAL]       = "ALERT_CRITICAL",
    [EVENT_ALERT_ACTIVE]         = "ALERT_ACTIVE",
    [EVENT_SENSOR_WARNING]       = "SENSOR_WARNING",
};

// Verify state_names array matches enum
_Static_assert(sizeof(state_names) / sizeof(state_names[0]) == EVENT_MAX,
               "state_names array size must match EVENT_MAX");

// Channel registration structure
typedef struct {
    const char *name;
    event_channel_cb_t cb;
    void *ctx;
} event_channel_t;

// Static context structure
static struct {
    bool initialized;
    uint32_t active_states;              // Bitmask of active states
    event_state_t current_active;        // Cached highest priority active state
    event_channel_t channels[CONFIG_EVENT_BUS_MAX_CHANNELS];
    size_t channel_count;
    SemaphoreHandle_t mutex;
} s_ctx = {
    .initialized = false,
    .active_states = 0,
    .current_active = EVENT_OFF,
    .channel_count = 0,
    .mutex = NULL,
};

// MOTD storage (protected by s_ctx.mutex)
static motd_entry_t s_motds[EVENT_BUS_MAX_MOTDS];
static size_t s_motd_count = 0;
static uint32_t s_motd_next_id = 1;

// --------------------------------------------------------------------------
// Helper Functions
// --------------------------------------------------------------------------

/**
 * @brief Get highest priority active state (lowest bit set)
 * @return Highest priority active state, or EVENT_OFF if none active
 */
static event_state_t get_highest_priority_state(void)
{
    if (s_ctx.active_states == 0) {
        return EVENT_OFF;
    }

    // Find lowest set bit (highest priority state)
    for (int i = 0; i < EVENT_MAX; i++) {
        if (s_ctx.active_states & (1U << i)) {
            return (event_state_t)i;
        }
    }

    // Should be unreachable if active_states != 0
    ESP_LOGE(TAG, "BUG: active_states=0x%08" PRIx32 " but no bit found in range 0-%d",
             s_ctx.active_states, EVENT_MAX - 1);
    return EVENT_OFF;
}

/**
 * @brief Notify all registered channels of state change
 *
 * Called with mutex held. Callbacks are invoked synchronously.
 *
 * @param new_state The new active state to notify channels about
 */
static void notify_channels(event_state_t new_state)
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
    event_state_t new_active = get_highest_priority_state();

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

esp_err_t event_bus_init(void)
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
    s_ctx.current_active = EVENT_OFF;
    s_ctx.channel_count = 0;
    memset(s_ctx.channels, 0, sizeof(s_ctx.channels));

    // Initialize MOTD storage
    memset(s_motds, 0, sizeof(s_motds));
    s_motd_count = 0;
    s_motd_next_id = 1;

    s_ctx.initialized = true;
    ESP_LOGI(TAG, "Initialized (max channels: %d)", CONFIG_EVENT_BUS_MAX_CHANNELS);

    return ESP_OK;
}

esp_err_t event_bus_deinit(void)
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
    s_ctx.current_active = EVENT_OFF;
    s_ctx.channel_count = 0;
    memset(s_ctx.channels, 0, sizeof(s_ctx.channels));

    // Clear MOTD storage
    memset(s_motds, 0, sizeof(s_motds));
    s_motd_count = 0;
    s_motd_next_id = 1;

    // Give back mutex before deleting
    xSemaphoreGive(s_ctx.mutex);
    vSemaphoreDelete(s_ctx.mutex);
    s_ctx.mutex = NULL;

    ESP_LOGI(TAG, "Deinitialized");
    return ESP_OK;
}

esp_err_t event_bus_set_state(event_state_t state)
{
    if (state >= EVENT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    // Check business state gate before acquiring mutex
    if (event_bus_is_business_state(state) && !system_state_is_claimed()) {
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

esp_err_t event_bus_clear_state(event_state_t state)
{
    if (state >= EVENT_MAX) {
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

event_state_t event_bus_get_active_state(void)
{
    if (!s_ctx.initialized) {
        return EVENT_OFF;
    }

    // Reading a single enum is atomic on ESP32
    return s_ctx.current_active;
}

esp_err_t event_bus_register_channel(const char *name, event_channel_cb_t cb, void *ctx)
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
    if (s_ctx.channel_count >= CONFIG_EVENT_BUS_MAX_CHANNELS) {
        ESP_LOGE(TAG, "Max channels (%d) reached, cannot register '%s'",
                 CONFIG_EVENT_BUS_MAX_CHANNELS, name);
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
             name, s_ctx.channel_count, CONFIG_EVENT_BUS_MAX_CHANNELS);

    // Notify new channel of current state immediately
    event_state_t current = s_ctx.current_active;
    xSemaphoreGive(s_ctx.mutex);

    // Call callback outside mutex to avoid deadlock
    ESP_LOGD(TAG, "Initial notify to '%s': %s", name, state_names[current]);
    cb(current, ctx);

    return ESP_OK;
}

// --------------------------------------------------------------------------
// Utility Functions
// --------------------------------------------------------------------------

bool event_bus_is_business_state(event_state_t state)
{
    return (state == EVENT_ALERT_CRITICAL ||
            state == EVENT_ALERT_ACTIVE ||
            state == EVENT_SENSOR_WARNING);
}

const char *event_state_name(event_state_t state)
{
    if (state >= EVENT_MAX) {
        return "UNKNOWN";
    }
    return state_names[state];
}

// --------------------------------------------------------------------------
// MOTD Functions
// --------------------------------------------------------------------------

esp_err_t event_bus_post_motd(const char *source, const char *message, motd_priority_t priority)
{
    if (source == NULL || message == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (priority >= MOTD_PRIORITY_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in post_motd");
        return ESP_ERR_TIMEOUT;
    }

    // Check for existing MOTD from same source with same message (update in place)
    for (size_t i = 0; i < s_motd_count; i++) {
        if (strncmp(s_motds[i].source, source, sizeof(s_motds[i].source)) == 0 &&
            strncmp(s_motds[i].message, message, sizeof(s_motds[i].message)) == 0) {
            // Update existing entry
            s_motds[i].priority = priority;
            s_motds[i].timestamp = (uint32_t)(esp_log_timestamp() / 1000);
            ESP_LOGD(TAG, "MOTD updated: [%s] %s (id=%" PRIu32 ")",
                     source, message, s_motds[i].id);
            xSemaphoreGive(s_ctx.mutex);
            return ESP_OK;
        }
    }

    // Check for available slot
    if (s_motd_count >= EVENT_BUS_MAX_MOTDS) {
        ESP_LOGW(TAG, "MOTD slots full (%d), cannot post from '%s'",
                 EVENT_BUS_MAX_MOTDS, source);
        xSemaphoreGive(s_ctx.mutex);
        return ESP_ERR_NO_MEM;
    }

    // Add new MOTD entry
    motd_entry_t *entry = &s_motds[s_motd_count];
    entry->id = s_motd_next_id++;
    strncpy(entry->source, source, sizeof(entry->source) - 1);
    entry->source[sizeof(entry->source) - 1] = '\0';
    strncpy(entry->message, message, sizeof(entry->message) - 1);
    entry->message[sizeof(entry->message) - 1] = '\0';
    entry->priority = priority;
    entry->timestamp = (uint32_t)(esp_log_timestamp() / 1000);
    s_motd_count++;

    ESP_LOGI(TAG, "MOTD posted: [%s] %s (id=%" PRIu32 ", %zu/%d)",
             source, message, entry->id, s_motd_count, EVENT_BUS_MAX_MOTDS);

    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

esp_err_t event_bus_get_motds(motd_entry_t *out, size_t max_count, size_t *count)
{
    if (out == NULL || count == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    *count = 0;

    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in get_motds");
        return ESP_ERR_TIMEOUT;
    }

    size_t to_copy = (s_motd_count < max_count) ? s_motd_count : max_count;
    if (to_copy > 0) {
        memcpy(out, s_motds, to_copy * sizeof(motd_entry_t));
    }
    *count = to_copy;

    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

esp_err_t event_bus_dismiss_motd(uint32_t id)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in dismiss_motd");
        return ESP_ERR_TIMEOUT;
    }

    // Find entry by ID
    for (size_t i = 0; i < s_motd_count; i++) {
        if (s_motds[i].id == id) {
            ESP_LOGI(TAG, "MOTD dismissed: [%s] %s (id=%" PRIu32 ")",
                     s_motds[i].source, s_motds[i].message, id);

            // Compact array: shift remaining entries down
            for (size_t j = i; j < s_motd_count - 1; j++) {
                s_motds[j] = s_motds[j + 1];
            }
            s_motd_count--;
            memset(&s_motds[s_motd_count], 0, sizeof(motd_entry_t));

            xSemaphoreGive(s_ctx.mutex);
            return ESP_OK;
        }
    }

    xSemaphoreGive(s_ctx.mutex);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t event_bus_clear_motds_from(const char *source)
{
    if (source == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in clear_motds_from");
        return ESP_ERR_TIMEOUT;
    }

    // Remove all entries matching source, compact as we go
    size_t write_idx = 0;
    size_t removed = 0;
    for (size_t read_idx = 0; read_idx < s_motd_count; read_idx++) {
        if (strncmp(s_motds[read_idx].source, source, sizeof(s_motds[read_idx].source)) == 0) {
            removed++;
        } else {
            if (write_idx != read_idx) {
                s_motds[write_idx] = s_motds[read_idx];
            }
            write_idx++;
        }
    }

    // Clear vacated slots
    for (size_t i = write_idx; i < s_motd_count; i++) {
        memset(&s_motds[i], 0, sizeof(motd_entry_t));
    }
    s_motd_count = write_idx;

    if (removed > 0) {
        ESP_LOGI(TAG, "Cleared %zu MOTDs from '%s' (%zu remaining)",
                 removed, source, s_motd_count);
    }

    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

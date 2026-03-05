/**
 * @file status_buzzer.c
 * @brief Status buzzer core implementation (event bus channel subscriber)
 *
 * Maps system states to buzzer patterns via buzzer_driver component
 * for hardware abstraction. Subscribes to event_bus for state
 * notifications and plays appropriate transition sounds.
 */

#include "status_buzzer.h"
#include "buzzer_patterns.h"
#include "buzzer_driver.h"
#include "event_bus.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"

static const char *TAG = "status_buzzer";

// NVS configuration
#define NVS_NAMESPACE       "buzzer"
#define NVS_KEY_ENABLED     "enabled"
#define NVS_KEY_ALERTS_ONLY "alerts_only"
#define NVS_KEY_VOL_CHIRP   "vol_chirp"
#define NVS_KEY_VOL_DOUBLE  "vol_double"
#define NVS_KEY_VOL_TRIPLE  "vol_triple"
#define NVS_KEY_VOL_ALARM   "vol_alarm"
#define NVS_KEY_VOL_SOLID   "vol_solid"

// Mutex timeout
#define MUTEX_TIMEOUT_MS 100
#define DEINIT_MUTEX_TIMEOUT_MS 500

// Preset names for logging
static const char *s_preset_names[] = {
    [BUZZER_PRESET_CHIRP]       = "CHIRP",
    [BUZZER_PRESET_DOUBLE_BEEP] = "DOUBLE_BEEP",
    [BUZZER_PRESET_TRIPLE_BEEP] = "TRIPLE_BEEP",
    [BUZZER_PRESET_ALARM]       = "ALARM",
    [BUZZER_PRESET_SOLID]       = "SOLID",
};

// Verify preset names array matches enum
_Static_assert(sizeof(s_preset_names) / sizeof(s_preset_names[0]) == BUZZER_PRESET_MAX,
               "s_preset_names array size must match BUZZER_PRESET_MAX");

// Static state structure
static struct {
    SemaphoreHandle_t mutex;
    bool initialized;
    bool enabled;
    bool alerts_only;
    uint8_t volumes[BUZZER_PRESET_MAX];
    event_state_t prev_state;
    bool alarm_active;  // Track if alarm is currently looping
} s_ctx = {
    .mutex = NULL,
    .initialized = false,
    .enabled = true,
    .alerts_only = false,
    .volumes = {
        [BUZZER_PRESET_CHIRP]       = CONFIG_STATUS_BUZZER_VOLUME_CHIRP,
        [BUZZER_PRESET_DOUBLE_BEEP] = CONFIG_STATUS_BUZZER_VOLUME_DOUBLE_BEEP,
        [BUZZER_PRESET_TRIPLE_BEEP] = CONFIG_STATUS_BUZZER_VOLUME_TRIPLE_BEEP,
        [BUZZER_PRESET_ALARM]       = CONFIG_STATUS_BUZZER_VOLUME_ALARM,
        [BUZZER_PRESET_SOLID]       = CONFIG_STATUS_BUZZER_VOLUME_SOLID,
    },
    .prev_state = EVENT_OFF,
    .alarm_active = false,
};

// --------------------------------------------------------------------------
// NVS Functions
// --------------------------------------------------------------------------

/**
 * @brief Load default configuration from Kconfig
 */
static void load_defaults(void)
{
    s_ctx.enabled = true;
    s_ctx.alerts_only = false;
    s_ctx.volumes[BUZZER_PRESET_CHIRP] = CONFIG_STATUS_BUZZER_VOLUME_CHIRP;
    s_ctx.volumes[BUZZER_PRESET_DOUBLE_BEEP] = CONFIG_STATUS_BUZZER_VOLUME_DOUBLE_BEEP;
    s_ctx.volumes[BUZZER_PRESET_TRIPLE_BEEP] = CONFIG_STATUS_BUZZER_VOLUME_TRIPLE_BEEP;
    s_ctx.volumes[BUZZER_PRESET_ALARM] = CONFIG_STATUS_BUZZER_VOLUME_ALARM;
    s_ctx.volumes[BUZZER_PRESET_SOLID] = CONFIG_STATUS_BUZZER_VOLUME_SOLID;
}

/**
 * @brief Load configuration from NVS
 *
 * Loads enabled, alerts_only, and per-preset volumes from NVS.
 * If NVS values don't exist or there's an error, keeps defaults.
 */
static void load_nvs_config(void)
{
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (ret != ESP_OK) {
        if (ret == ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGI(TAG, "No saved config found, using defaults");
        } else {
            ESP_LOGW(TAG, "Failed to open NVS for reading: %s", esp_err_to_name(ret));
        }
        return;
    }

    // Load enabled state
    uint8_t enabled_val;
    ret = nvs_get_u8(handle, NVS_KEY_ENABLED, &enabled_val);
    if (ret == ESP_OK) {
        s_ctx.enabled = (enabled_val != 0);
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read '%s' from NVS: %s", NVS_KEY_ENABLED, esp_err_to_name(ret));
    }

    // Load alerts_only state
    uint8_t alerts_only_val;
    ret = nvs_get_u8(handle, NVS_KEY_ALERTS_ONLY, &alerts_only_val);
    if (ret == ESP_OK) {
        s_ctx.alerts_only = (alerts_only_val != 0);
    } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "Failed to read '%s' from NVS: %s", NVS_KEY_ALERTS_ONLY, esp_err_to_name(ret));
    }

    // Load per-preset volumes
    static const struct {
        const char *key;
        buzzer_preset_t preset;
    } vol_keys[] = {
        {NVS_KEY_VOL_CHIRP, BUZZER_PRESET_CHIRP},
        {NVS_KEY_VOL_DOUBLE, BUZZER_PRESET_DOUBLE_BEEP},
        {NVS_KEY_VOL_TRIPLE, BUZZER_PRESET_TRIPLE_BEEP},
        {NVS_KEY_VOL_ALARM, BUZZER_PRESET_ALARM},
        {NVS_KEY_VOL_SOLID, BUZZER_PRESET_SOLID},
    };

    for (size_t i = 0; i < sizeof(vol_keys) / sizeof(vol_keys[0]); i++) {
        uint8_t vol_val;
        ret = nvs_get_u8(handle, vol_keys[i].key, &vol_val);
        if (ret == ESP_OK) {
            if (vol_val > 100) {
                ESP_LOGW(TAG, "Volume %s=%d invalid, clamping to 100", vol_keys[i].key, vol_val);
                vol_val = 100;
            }
            s_ctx.volumes[vol_keys[i].preset] = vol_val;
        } else if (ret != ESP_ERR_NVS_NOT_FOUND) {
            ESP_LOGW(TAG, "Failed to read '%s' from NVS: %s", vol_keys[i].key, esp_err_to_name(ret));
        }
    }

    nvs_close(handle);

    ESP_LOGI(TAG, "Loaded config: enabled=%d, alerts_only=%d, volumes=[%d,%d,%d,%d,%d]",
             s_ctx.enabled, s_ctx.alerts_only,
             s_ctx.volumes[0], s_ctx.volumes[1], s_ctx.volumes[2],
             s_ctx.volumes[3], s_ctx.volumes[4]);
}

/**
 * @brief Save configuration to NVS
 */
static esp_err_t save_config(void)
{
    nvs_handle_t handle;
    esp_err_t ret;

    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS for writing: %s", esp_err_to_name(ret));
        return ret;
    }

    // Save enabled state
    ret = nvs_set_u8(handle, NVS_KEY_ENABLED, s_ctx.enabled ? 1 : 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save enabled: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    // Save alerts_only state
    ret = nvs_set_u8(handle, NVS_KEY_ALERTS_ONLY, s_ctx.alerts_only ? 1 : 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to save alerts_only: %s", esp_err_to_name(ret));
        nvs_close(handle);
        return ret;
    }

    // Save per-preset volumes
    static const struct {
        const char *key;
        buzzer_preset_t preset;
    } vol_keys[] = {
        {NVS_KEY_VOL_CHIRP, BUZZER_PRESET_CHIRP},
        {NVS_KEY_VOL_DOUBLE, BUZZER_PRESET_DOUBLE_BEEP},
        {NVS_KEY_VOL_TRIPLE, BUZZER_PRESET_TRIPLE_BEEP},
        {NVS_KEY_VOL_ALARM, BUZZER_PRESET_ALARM},
        {NVS_KEY_VOL_SOLID, BUZZER_PRESET_SOLID},
    };

    for (size_t i = 0; i < sizeof(vol_keys) / sizeof(vol_keys[0]); i++) {
        ret = nvs_set_u8(handle, vol_keys[i].key, s_ctx.volumes[vol_keys[i].preset]);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to save %s: %s", vol_keys[i].key, esp_err_to_name(ret));
            nvs_close(handle);
            return ret;
        }
    }

    ret = nvs_commit(handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to commit NVS: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGI(TAG, "Config saved to NVS");
    }

    nvs_close(handle);
    return ret;
}

// --------------------------------------------------------------------------
// Helper Functions
// --------------------------------------------------------------------------

/**
 * @brief Check if a state is an alert state (plays sounds even in alerts_only mode)
 */
static bool is_alert_state(event_state_t state)
{
    return (state == EVENT_ALERT_CRITICAL ||
            state == EVENT_ALERT_ACTIVE ||
            state == EVENT_ERROR);
}

/**
 * @brief Check if state transition should be silent
 *
 * Silent transitions: WIFI_PROVISIONING, WIFI_RECONNECTING, WIFI_CONNECTING,
 * TAILSCALE_CONNECTING, EVENT_OFF
 */
static bool is_silent_transition(event_state_t state)
{
    switch (state) {
        case EVENT_WIFI_PROVISIONING:
        case EVENT_WIFI_RECONNECTING:
        case EVENT_WIFI_CONNECTING:
        case EVENT_TAILSCALE_CONNECTING:
        case EVENT_OFF:
            return true;
        default:
            return false;
    }
}

/**
 * @brief Play a preset with current volume (internal, no lock)
 */
static esp_err_t play_preset_unlocked(buzzer_preset_t preset)
{
    if (!s_ctx.enabled) {
        return ESP_OK;  // Silently succeed when disabled
    }

    uint8_t volume = s_ctx.volumes[preset];
    ESP_LOGI(TAG, "Playing: %s (volume=%d%%)", s_preset_names[preset], volume);

    esp_err_t ret = buzzer_pattern_play(preset, volume);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to play %s: %s", s_preset_names[preset], esp_err_to_name(ret));
    }

    // Track if we started an alarm (looping pattern)
    s_ctx.alarm_active = (preset == BUZZER_PRESET_ALARM);

    return ret;
}

/**
 * @brief Event bus channel callback - plays transition sounds
 *
 * Called by event_bus when the active state changes. Maps state transitions
 * to buzzer patterns and plays appropriate sounds.
 *
 * @param new_state New active event state
 * @param ctx User context (unused)
 */
static void on_event_state_change(event_state_t new_state, void *ctx)
{
    (void)ctx;

    if (!s_ctx.initialized) {
        return;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGW(TAG, "Mutex timeout in event bus callback");
        return;
    }

    event_state_t prev_state = s_ctx.prev_state;
    s_ctx.prev_state = new_state;

    // Skip if disabled
    if (!s_ctx.enabled) {
        xSemaphoreGive(s_ctx.mutex);
        return;
    }

    ESP_LOGD(TAG, "State transition: %s -> %s",
             event_state_name(prev_state), event_state_name(new_state));

    // Check if we're clearing an alert state
    bool was_alert = is_alert_state(prev_state);
    bool is_alert = is_alert_state(new_state);

    if (was_alert && !is_alert) {
        // Alert cleared - stop alarm immediately and play ack sound
        if (s_ctx.alarm_active) {
            ESP_LOGI(TAG, "Alert cleared, stopping alarm");
            buzzer_pattern_stop();
            s_ctx.alarm_active = false;
        }
        // Play acknowledgment chirp unless in alerts_only mode
        if (!s_ctx.alerts_only) {
            play_preset_unlocked(BUZZER_PRESET_CHIRP);
        }
        xSemaphoreGive(s_ctx.mutex);
        return;
    }

    // Check if FIRST_BOOT was just cleared (device claimed)
    if (prev_state == EVENT_FIRST_BOOT && new_state != EVENT_FIRST_BOOT) {
        // Device was just claimed - play celebration sound
        ESP_LOGI(TAG, "Device claimed, playing celebration");
        if (!s_ctx.alerts_only) {
            play_preset_unlocked(BUZZER_PRESET_TRIPLE_BEEP);
        }
        xSemaphoreGive(s_ctx.mutex);
        return;
    }

    // Skip silent transitions
    if (is_silent_transition(new_state)) {
        xSemaphoreGive(s_ctx.mutex);
        return;
    }

    // Check alerts_only mode for non-alert sounds
    if (s_ctx.alerts_only && !is_alert) {
        xSemaphoreGive(s_ctx.mutex);
        return;
    }

    // Map state to sound
    buzzer_preset_t preset;
    switch (new_state) {
        case EVENT_CONNECTED:
            preset = BUZZER_PRESET_DOUBLE_BEEP;
            break;

        case EVENT_ERROR:
            preset = BUZZER_PRESET_TRIPLE_BEEP;
            break;

        case EVENT_ALERT_CRITICAL:
            preset = BUZZER_PRESET_ALARM;  // Loops until cleared
            break;

        case EVENT_ALERT_ACTIVE:
            preset = BUZZER_PRESET_DOUBLE_BEEP;
            break;

        case EVENT_SENSOR_WARNING:
            preset = BUZZER_PRESET_CHIRP;
            break;

        default:
            // No sound for other states
            xSemaphoreGive(s_ctx.mutex);
            return;
    }

    play_preset_unlocked(preset);

    xSemaphoreGive(s_ctx.mutex);
}

// --------------------------------------------------------------------------
// Public API Implementation
// --------------------------------------------------------------------------

esp_err_t status_buzzer_init(void)
{
    if (s_ctx.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Create mutex
    s_ctx.mutex = xSemaphoreCreateMutex();
    if (s_ctx.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Load defaults, then override with NVS config
    load_defaults();
    load_nvs_config();

    // Initialize buzzer driver
    esp_err_t ret = buzzer_driver_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize buzzer driver: %s", esp_err_to_name(ret));
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return ret;
    }

    // Initialize pattern player
    ret = buzzer_pattern_player_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize pattern player: %s", esp_err_to_name(ret));
        buzzer_driver_deinit();
        vSemaphoreDelete(s_ctx.mutex);
        s_ctx.mutex = NULL;
        return ret;
    }

    s_ctx.initialized = true;
    s_ctx.prev_state = EVENT_OFF;
    s_ctx.alarm_active = false;

    ESP_LOGI(TAG, "Initialized (enabled=%d, alerts_only=%d)",
             s_ctx.enabled, s_ctx.alerts_only);

    // Register as event bus channel
    ret = event_bus_register_channel("buzzer", on_event_state_change, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register event bus channel: %s", esp_err_to_name(ret));
        // Continue anyway - manual play still works
    } else {
        ESP_LOGI(TAG, "Registered as event bus channel");
    }

    // Play boot chirp if enabled and not in alerts_only mode
    if (s_ctx.enabled && !s_ctx.alerts_only) {
        ESP_LOGI(TAG, "Playing boot chirp");
        status_buzzer_play(BUZZER_PRESET_CHIRP);
    }

    return ESP_OK;
}

esp_err_t status_buzzer_deinit(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(DEINIT_MUTEX_TIMEOUT_MS)) != pdTRUE) {
        ESP_LOGE(TAG, "Mutex timeout during deinit");
        return ESP_ERR_TIMEOUT;
    }

    // Stop any playing sound
    buzzer_pattern_stop();
    s_ctx.alarm_active = false;

    s_ctx.initialized = false;

    xSemaphoreGive(s_ctx.mutex);
    vSemaphoreDelete(s_ctx.mutex);
    s_ctx.mutex = NULL;

    // Deinitialize pattern player and driver
    buzzer_pattern_player_deinit();
    buzzer_driver_deinit();

    ESP_LOGI(TAG, "Deinitialized");
    return ESP_OK;
}

esp_err_t status_buzzer_set_enabled(bool enabled)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_ctx.enabled = enabled;
    ESP_LOGI(TAG, "Status buzzer %s", enabled ? "enabled" : "disabled");

    if (!enabled) {
        // Stop any playing sound when disabled
        buzzer_pattern_stop();
        s_ctx.alarm_active = false;
    }

    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

bool status_buzzer_is_enabled(void)
{
    return s_ctx.enabled;
}

esp_err_t status_buzzer_set_alerts_only(bool alerts_only)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_ctx.alerts_only = alerts_only;
    ESP_LOGI(TAG, "Alerts-only mode %s", alerts_only ? "enabled" : "disabled");

    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

bool status_buzzer_is_alerts_only(void)
{
    return s_ctx.alerts_only;
}

esp_err_t status_buzzer_set_preset_volume(buzzer_preset_t preset, uint8_t percent)
{
    if (preset >= BUZZER_PRESET_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    s_ctx.volumes[preset] = percent;
    ESP_LOGI(TAG, "Preset %s volume set to %d%%", s_preset_names[preset], percent);

    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

uint8_t status_buzzer_get_preset_volume(buzzer_preset_t preset)
{
    if (preset >= BUZZER_PRESET_MAX) {
        return 0;
    }
    return s_ctx.volumes[preset];
}

esp_err_t status_buzzer_play(buzzer_preset_t preset)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (preset >= BUZZER_PRESET_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = play_preset_unlocked(preset);

    xSemaphoreGive(s_ctx.mutex);
    return ret;
}

esp_err_t status_buzzer_stop(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    buzzer_pattern_stop();
    s_ctx.alarm_active = false;
    ESP_LOGD(TAG, "Stopped");

    xSemaphoreGive(s_ctx.mutex);
    return ESP_OK;
}

esp_err_t status_buzzer_save_config(void)
{
    if (!s_ctx.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_ctx.mutex, pdMS_TO_TICKS(MUTEX_TIMEOUT_MS)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = save_config();

    xSemaphoreGive(s_ctx.mutex);
    return ret;
}

const char *buzzer_preset_name(buzzer_preset_t preset)
{
    if (preset >= BUZZER_PRESET_MAX) {
        return "UNKNOWN";
    }
    return s_preset_names[preset];
}

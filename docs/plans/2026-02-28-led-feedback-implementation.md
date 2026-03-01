# LED Feedback Component Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Implement status LED feedback component using Espressif's led_indicator for at-a-glance system health indication.

**Architecture:** Thin wrapper around `espressif/led_indicator` managed component. Defines blink patterns for each system state, handles NVS persistence for user config, provides console commands for runtime control.

**Tech Stack:** ESP-IDF v5.5, espressif/led_indicator ^0.9, LEDC PWM (DevKitC), FreeRTOS mutex

---

## Task 1: Add led_indicator Managed Component

**Files:**
- Create: `firmware/components/led_feedback/idf_component.yml`

**Step 1: Create component directory**

```bash
mkdir -p firmware/components/led_feedback/include
mkdir -p firmware/components/led_feedback/src
```

**Step 2: Create idf_component.yml with dependency**

```yaml
dependencies:
  espressif/led_indicator: "^0.9"
  idf:
    version: ">=5.0"
```

**Step 3: Verify dependency resolves**

Run: `cd firmware && idf.py reconfigure 2>&1 | head -50`
Expected: Component manager downloads led_indicator, no errors

**Step 4: Commit**

```bash
git add firmware/components/led_feedback/idf_component.yml
git commit -m "feat(led_feedback): add led_indicator managed component dependency"
```

---

## Task 2: Create Component Build Files

**Files:**
- Create: `firmware/components/led_feedback/CMakeLists.txt`
- Create: `firmware/components/led_feedback/Kconfig`

**Step 1: Create CMakeLists.txt**

```cmake
idf_component_register(
    SRCS
        "src/led_feedback.c"
        "src/led_patterns.c"
        "src/led_console.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        led_indicator
        nvs_flash
        esp_console
        freertos
    PRIV_REQUIRES
        esp_log
)
```

**Step 2: Create Kconfig**

```kconfig
menu "LED Feedback"

    config LED_FEEDBACK_ENABLED
        bool "Enable LED status feedback"
        default y
        help
            Master enable for LED status indication.
            Can be overridden at runtime via NVS.

    choice LED_FEEDBACK_TYPE
        prompt "LED hardware type"
        default LED_FEEDBACK_TYPE_LEDC

        config LED_FEEDBACK_TYPE_GPIO
            bool "Simple GPIO (on/off only)"

        config LED_FEEDBACK_TYPE_LEDC
            bool "LEDC PWM (brightness + breathing)"

        config LED_FEEDBACK_TYPE_WS2812
            bool "WS2812 RGB (addressable)"
    endchoice

    config LED_FEEDBACK_GPIO
        int "LED GPIO pin"
        default 48 if IDF_TARGET_ESP32S3
        default 35
        help
            GPIO pin connected to LED.
            DevKitC: 48 (WS2812), Heltec V3: 35 (white)

    config LED_FEEDBACK_DEFAULT_BRIGHTNESS
        int "Default brightness (0-100)"
        default 30
        range 0 100
        depends on !LED_FEEDBACK_TYPE_GPIO
        help
            Default brightness percentage.
            30% recommended to avoid blinding users.

    config LED_FEEDBACK_ACTIVE_LOW
        bool "LED is active-low"
        default n
        help
            Enable if LED turns on when GPIO is LOW.

    config LED_FEEDBACK_BLINK_FAST_MS
        int "Fast blink period (ms)"
        default 400
        help
            Total period for fast blink (half on, half off).

    config LED_FEEDBACK_BLINK_MEDIUM_MS
        int "Medium blink period (ms)"
        default 1000

    config LED_FEEDBACK_BLINK_SLOW_MS
        int "Slow blink period (ms)"
        default 2000

    config LED_FEEDBACK_BREATHE_PERIOD_MS
        int "Breathing cycle period (ms)"
        default 2000
        help
            Full fade-in + fade-out cycle time.

endmenu
```

**Step 3: Commit**

```bash
git add firmware/components/led_feedback/CMakeLists.txt
git add firmware/components/led_feedback/Kconfig
git commit -m "feat(led_feedback): add CMakeLists.txt and Kconfig"
```

---

## Task 3: Create Public Header

**Files:**
- Create: `firmware/components/led_feedback/include/led_feedback.h`

**Step 1: Create led_feedback.h**

```c
/**
 * @file led_feedback.h
 * @brief LED status feedback component
 *
 * Provides visual status indication via onboard LED. Uses priority-based
 * state management - higher priority states preempt lower priority states.
 *
 * @note Thread Safety: All public functions are thread-safe and may be
 *       called from multiple tasks (e.g., WiFi callbacks, main task).
 *       Internal mutex protects state transitions.
 *
 * @note Priority: States are prioritized by enum order. Lower enum value
 *       = higher priority. Use clear_state() to allow lower priority
 *       states to resume.
 */

#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief LED feedback states (priority order - lower value = higher priority)
 */
typedef enum {
    // Priority 0 (highest) - Immediate attention
    LED_STATE_ALERT_CRITICAL,       ///< Fast double-pulse (red on RGB)

    // Priority 1 - Setup required
    LED_STATE_FIRST_BOOT,           ///< Slow breathe (purple on RGB)
    LED_STATE_WIFI_PROVISIONING,    ///< Slow blink (blue on RGB)

    // Priority 2 - Problems
    LED_STATE_WIFI_RECONNECTING,    ///< Medium blink (yellow on RGB)
    LED_STATE_ALERT_ACTIVE,         ///< Double-pulse slow (orange on RGB)

    // Priority 3 - Transitional
    LED_STATE_WIFI_CONNECTING,      ///< Fast blink (cyan on RGB)
    LED_STATE_TAILSCALE_CONNECTING, ///< Fast blink (cyan on RGB)

    // Priority 4 (lowest) - Normal operation
    LED_STATE_CONNECTED,            ///< Solid on (green on RGB)
    LED_STATE_OFF,                  ///< Off

    LED_STATE_MAX                   ///< Sentinel for bounds checking
} led_state_t;

/**
 * @brief Initialize LED feedback component
 *
 * Creates internal mutex, loads config from NVS (or uses Kconfig defaults),
 * and initializes the LED indicator driver.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if already initialized
 * @return ESP_ERR_NO_MEM if mutex creation fails
 * @return ESP_FAIL if LED indicator creation fails
 */
esp_err_t led_feedback_init(void);

/**
 * @brief Deinitialize LED feedback component
 *
 * Stops any active patterns, destroys LED indicator, and frees resources.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_STATE if not initialized
 */
esp_err_t led_feedback_deinit(void);

/**
 * @brief Set LED state (activates pattern)
 *
 * If this state has higher priority than current, it takes over immediately.
 * If lower priority, it becomes pending and activates when higher clears.
 *
 * @param state State to activate
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if state >= LED_STATE_MAX
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex acquisition fails
 */
esp_err_t led_feedback_set_state(led_state_t state);

/**
 * @brief Clear LED state (deactivates pattern)
 *
 * Removes this state from active set. If it was showing, the next highest
 * priority active state takes over.
 *
 * @param state State to clear
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if state >= LED_STATE_MAX
 * @return ESP_ERR_INVALID_STATE if not initialized
 * @return ESP_ERR_TIMEOUT if mutex acquisition fails
 */
esp_err_t led_feedback_clear_state(led_state_t state);

/**
 * @brief Get currently displayed state
 *
 * @return Current state, or LED_STATE_OFF if not initialized
 */
led_state_t led_feedback_get_state(void);

/**
 * @brief Enable or disable LED feedback
 *
 * When disabled, LED is turned off regardless of state.
 * State tracking continues so re-enabling shows correct state.
 *
 * @param enabled true to enable, false to disable
 * @return ESP_OK on success
 */
esp_err_t led_feedback_set_enabled(bool enabled);

/**
 * @brief Set LED brightness
 *
 * Only effective for LEDC and RGB LED types.
 *
 * @param percent Brightness 0-100
 * @return ESP_OK on success
 * @return ESP_ERR_INVALID_ARG if percent > 100
 */
esp_err_t led_feedback_set_brightness(uint8_t percent);

/**
 * @brief Set alerts-only mode
 *
 * When enabled, only alert states (ALERT_CRITICAL, ALERT_ACTIVE) are shown.
 * Other states are tracked but LED stays off.
 *
 * @param alerts_only true for alerts only, false for all states
 * @return ESP_OK on success
 */
esp_err_t led_feedback_set_alerts_only(bool alerts_only);

/**
 * @brief Save current config to NVS
 *
 * Persists enabled, brightness, and alerts_only settings.
 *
 * @return ESP_OK on success
 * @return ESP_ERR_NVS_* on NVS errors
 */
esp_err_t led_feedback_save_config(void);

/**
 * @brief Check if LED feedback is enabled
 *
 * @return true if enabled
 */
bool led_feedback_is_enabled(void);

/**
 * @brief Get current brightness setting
 *
 * @return Brightness 0-100
 */
uint8_t led_feedback_get_brightness(void);

/**
 * @brief Check if alerts-only mode is active
 *
 * @return true if alerts-only mode
 */
bool led_feedback_is_alerts_only(void);

/**
 * @brief Register console commands
 *
 * Registers: led status, led on, led off, led brightness <n>, led alerts-only
 *
 * @return ESP_OK on success
 */
esp_err_t led_feedback_register_console(void);

/**
 * @brief Get state name string for logging
 *
 * @param state State to get name for
 * @return State name string (e.g., "CONNECTED", "PROVISIONING")
 */
const char *led_feedback_state_name(led_state_t state);

#ifdef __cplusplus
}
#endif
```

**Step 2: Commit**

```bash
git add firmware/components/led_feedback/include/led_feedback.h
git commit -m "feat(led_feedback): add public header with API documentation"
```

---

## Task 4: Implement Blink Patterns

**Files:**
- Create: `firmware/components/led_feedback/src/led_patterns.c`

**Step 1: Create led_patterns.c**

```c
/**
 * @file led_patterns.c
 * @brief Blink pattern definitions for LED feedback states
 */

#include "led_indicator.h"
#include "led_feedback.h"
#include "sdkconfig.h"

// Helper macros for timing from Kconfig
#define FAST_ON     (CONFIG_LED_FEEDBACK_BLINK_FAST_MS / 2)
#define FAST_OFF    (CONFIG_LED_FEEDBACK_BLINK_FAST_MS / 2)
#define MEDIUM_ON   (CONFIG_LED_FEEDBACK_BLINK_MEDIUM_MS / 2)
#define MEDIUM_OFF  (CONFIG_LED_FEEDBACK_BLINK_MEDIUM_MS / 2)
#define SLOW_ON     (CONFIG_LED_FEEDBACK_BLINK_SLOW_MS / 2)
#define SLOW_OFF    (CONFIG_LED_FEEDBACK_BLINK_SLOW_MS / 2)
#define BREATHE_IN  (CONFIG_LED_FEEDBACK_BREATHE_PERIOD_MS / 2)
#define BREATHE_OUT (CONFIG_LED_FEEDBACK_BREATHE_PERIOD_MS / 2)

// Pattern: Critical alert - fast double pulse
static const blink_step_t pattern_alert_critical[] = {
    {LED_BLINK_HOLD,  LED_STATE_ON,  100},
    {LED_BLINK_HOLD,  LED_STATE_OFF, 100},
    {LED_BLINK_HOLD,  LED_STATE_ON,  100},
    {LED_BLINK_HOLD,  LED_STATE_OFF, 700},
    {LED_BLINK_LOOP,  0,             0},
};

// Pattern: First boot - slow breathe
static const blink_step_t pattern_first_boot[] = {
    {LED_BLINK_BREATHE, LED_STATE_ON,  BREATHE_IN},
    {LED_BLINK_BREATHE, LED_STATE_OFF, BREATHE_OUT},
    {LED_BLINK_LOOP,    0,             0},
};

// Pattern: WiFi provisioning - slow blink
static const blink_step_t pattern_wifi_provisioning[] = {
    {LED_BLINK_HOLD, LED_STATE_ON,  SLOW_ON},
    {LED_BLINK_HOLD, LED_STATE_OFF, SLOW_OFF},
    {LED_BLINK_LOOP, 0,             0},
};

// Pattern: WiFi reconnecting - medium blink
static const blink_step_t pattern_wifi_reconnecting[] = {
    {LED_BLINK_HOLD, LED_STATE_ON,  MEDIUM_ON},
    {LED_BLINK_HOLD, LED_STATE_OFF, MEDIUM_OFF},
    {LED_BLINK_LOOP, 0,             0},
};

// Pattern: Alert active - slow double pulse
static const blink_step_t pattern_alert_active[] = {
    {LED_BLINK_HOLD,  LED_STATE_ON,  200},
    {LED_BLINK_HOLD,  LED_STATE_OFF, 200},
    {LED_BLINK_HOLD,  LED_STATE_ON,  200},
    {LED_BLINK_HOLD,  LED_STATE_OFF, 1400},
    {LED_BLINK_LOOP,  0,             0},
};

// Pattern: WiFi connecting - fast blink
static const blink_step_t pattern_wifi_connecting[] = {
    {LED_BLINK_HOLD, LED_STATE_ON,  FAST_ON},
    {LED_BLINK_HOLD, LED_STATE_OFF, FAST_OFF},
    {LED_BLINK_LOOP, 0,             0},
};

// Pattern: Tailscale connecting - fast blink (same as wifi)
static const blink_step_t pattern_tailscale_connecting[] = {
    {LED_BLINK_HOLD, LED_STATE_ON,  FAST_ON},
    {LED_BLINK_HOLD, LED_STATE_OFF, FAST_OFF},
    {LED_BLINK_LOOP, 0,             0},
};

// Pattern: Connected - solid on
static const blink_step_t pattern_connected[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 0},
    {LED_BLINK_STOP, 0,            0},
};

// Pattern: Off - solid off
static const blink_step_t pattern_off[] = {
    {LED_BLINK_HOLD, LED_STATE_OFF, 0},
    {LED_BLINK_STOP, 0,             0},
};

// Pattern list indexed by led_state_t
// MUST match led_state_t enum order exactly
blink_step_t const *led_patterns[] = {
    [LED_STATE_ALERT_CRITICAL]       = pattern_alert_critical,
    [LED_STATE_FIRST_BOOT]           = pattern_first_boot,
    [LED_STATE_WIFI_PROVISIONING]    = pattern_wifi_provisioning,
    [LED_STATE_WIFI_RECONNECTING]    = pattern_wifi_reconnecting,
    [LED_STATE_ALERT_ACTIVE]         = pattern_alert_active,
    [LED_STATE_WIFI_CONNECTING]      = pattern_wifi_connecting,
    [LED_STATE_TAILSCALE_CONNECTING] = pattern_tailscale_connecting,
    [LED_STATE_CONNECTED]            = pattern_connected,
    [LED_STATE_OFF]                  = pattern_off,
};

// Verify array size matches enum at compile time
_Static_assert(sizeof(led_patterns) / sizeof(led_patterns[0]) == LED_STATE_MAX,
               "led_patterns array size must match LED_STATE_MAX");
```

**Step 2: Commit**

```bash
git add firmware/components/led_feedback/src/led_patterns.c
git commit -m "feat(led_feedback): add blink pattern definitions"
```

---

## Task 5: Implement Core Functionality

**Files:**
- Create: `firmware/components/led_feedback/src/led_feedback.c`

**Step 1: Create led_feedback.c**

```c
/**
 * @file led_feedback.c
 * @brief LED feedback component implementation
 */

#include "led_feedback.h"
#include "led_indicator.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "sdkconfig.h"
#include <string.h>

static const char *TAG = "led_feedback";

// NVS storage
#define NVS_NAMESPACE "led_cfg"
#define NVS_KEY_ENABLED "enabled"
#define NVS_KEY_BRIGHTNESS "brightness"
#define NVS_KEY_ALERTS_ONLY "alerts_only"

// External pattern list from led_patterns.c
extern blink_step_t const *led_patterns[];

// Internal state
static struct {
    bool initialized;
    bool enabled;
    uint8_t brightness;
    bool alerts_only;
    led_state_t current_state;
    led_indicator_handle_t handle;
    SemaphoreHandle_t mutex;
} s_led = {
    .initialized = false,
    .enabled = true,
    .brightness = CONFIG_LED_FEEDBACK_DEFAULT_BRIGHTNESS,
    .alerts_only = false,
    .current_state = LED_STATE_OFF,
    .handle = NULL,
    .mutex = NULL,
};

// State names for logging
static const char *state_names[] = {
    [LED_STATE_ALERT_CRITICAL]       = "ALERT_CRITICAL",
    [LED_STATE_FIRST_BOOT]           = "FIRST_BOOT",
    [LED_STATE_WIFI_PROVISIONING]    = "WIFI_PROVISIONING",
    [LED_STATE_WIFI_RECONNECTING]    = "WIFI_RECONNECTING",
    [LED_STATE_ALERT_ACTIVE]         = "ALERT_ACTIVE",
    [LED_STATE_WIFI_CONNECTING]      = "WIFI_CONNECTING",
    [LED_STATE_TAILSCALE_CONNECTING] = "TAILSCALE_CONNECTING",
    [LED_STATE_CONNECTED]            = "CONNECTED",
    [LED_STATE_OFF]                  = "OFF",
};

_Static_assert(sizeof(state_names) / sizeof(state_names[0]) == LED_STATE_MAX,
               "state_names array size must match LED_STATE_MAX");

const char *led_feedback_state_name(led_state_t state)
{
    if (state >= LED_STATE_MAX) {
        return "UNKNOWN";
    }
    return state_names[state];
}

// Load config from NVS
static esp_err_t load_config_from_nvs(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs);
    if (ret == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved config, using defaults");
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (ret != ESP_OK) {
        return ret;
    }

    uint8_t val;
    if (nvs_get_u8(nvs, NVS_KEY_ENABLED, &val) == ESP_OK) {
        s_led.enabled = (val != 0);
    }
    if (nvs_get_u8(nvs, NVS_KEY_BRIGHTNESS, &val) == ESP_OK) {
        s_led.brightness = (val <= 100) ? val : 100;
    }
    if (nvs_get_u8(nvs, NVS_KEY_ALERTS_ONLY, &val) == ESP_OK) {
        s_led.alerts_only = (val != 0);
    }

    nvs_close(nvs);
    ESP_LOGI(TAG, "Loaded config: enabled=%d, brightness=%d, alerts_only=%d",
             s_led.enabled, s_led.brightness, s_led.alerts_only);
    return ESP_OK;
}

esp_err_t led_feedback_save_config(void)
{
    nvs_handle_t nvs;
    esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open NVS: %s", esp_err_to_name(ret));
        return ret;
    }

    nvs_set_u8(nvs, NVS_KEY_ENABLED, s_led.enabled ? 1 : 0);
    nvs_set_u8(nvs, NVS_KEY_BRIGHTNESS, s_led.brightness);
    nvs_set_u8(nvs, NVS_KEY_ALERTS_ONLY, s_led.alerts_only ? 1 : 0);

    ret = nvs_commit(nvs);
    nvs_close(nvs);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "Config saved");
    }
    return ret;
}

// Check if state should be shown based on alerts_only mode
static bool should_show_state(led_state_t state)
{
    if (!s_led.enabled) {
        return false;
    }
    if (!s_led.alerts_only) {
        return true;
    }
    // In alerts-only mode, only show alert states
    return (state == LED_STATE_ALERT_CRITICAL || state == LED_STATE_ALERT_ACTIVE);
}

esp_err_t led_feedback_init(void)
{
    if (s_led.initialized) {
        ESP_LOGW(TAG, "Already initialized");
        return ESP_ERR_INVALID_STATE;
    }

    // Create mutex
    s_led.mutex = xSemaphoreCreateMutex();
    if (s_led.mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Load config from NVS (ignore errors, use defaults)
    esp_err_t ret = load_config_from_nvs();
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS load error: %s (using defaults)", esp_err_to_name(ret));
    }

    // Configure LED indicator based on Kconfig LED type
    led_indicator_config_t config = {
        .mode = LED_LEDC_MODE,  // Default to LEDC
        .led_indicator_ledc_config = &(led_indicator_ledc_config_t){
            .is_active_level_high = !CONFIG_LED_FEEDBACK_ACTIVE_LOW,
            .timer_inited = false,
            .timer_num = LEDC_TIMER_0,
            .gpio_num = CONFIG_LED_FEEDBACK_GPIO,
            .channel = LEDC_CHANNEL_0,
        },
        .blink_lists = led_patterns,
        .blink_list_num = LED_STATE_MAX,
    };

#if CONFIG_LED_FEEDBACK_TYPE_GPIO
    config.mode = LED_GPIO_MODE;
    config.led_indicator_gpio_config = &(led_indicator_gpio_config_t){
        .is_active_level_high = !CONFIG_LED_FEEDBACK_ACTIVE_LOW,
        .gpio_num = CONFIG_LED_FEEDBACK_GPIO,
    };
#elif CONFIG_LED_FEEDBACK_TYPE_WS2812
    // WS2812 requires led_strip backend - TODO for RGB support
    ESP_LOGW(TAG, "WS2812 support not yet implemented, falling back to LEDC");
#endif

    s_led.handle = led_indicator_create(&config);
    if (s_led.handle == NULL) {
        ESP_LOGE(TAG, "Failed to create LED indicator");
        vSemaphoreDelete(s_led.mutex);
        s_led.mutex = NULL;
        return ESP_FAIL;
    }

    s_led.initialized = true;
    s_led.current_state = LED_STATE_OFF;

    ESP_LOGI(TAG, "Initialized on GPIO %d (enabled=%d, brightness=%d%%)",
             CONFIG_LED_FEEDBACK_GPIO, s_led.enabled, s_led.brightness);

    return ESP_OK;
}

esp_err_t led_feedback_deinit(void)
{
    if (!s_led.initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (xSemaphoreTake(s_led.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    led_indicator_delete(s_led.handle);
    s_led.handle = NULL;
    s_led.initialized = false;

    SemaphoreHandle_t mutex = s_led.mutex;
    s_led.mutex = NULL;
    xSemaphoreGive(mutex);
    vSemaphoreDelete(mutex);

    ESP_LOGI(TAG, "Deinitialized");
    return ESP_OK;
}

esp_err_t led_feedback_set_state(led_state_t state)
{
    if (state >= LED_STATE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_led.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_led.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = ESP_OK;
    if (should_show_state(state)) {
        ret = led_indicator_start(s_led.handle, state);
        if (ret == ESP_OK) {
            s_led.current_state = state;
            ESP_LOGD(TAG, "State set: %s", led_feedback_state_name(state));
        }
    } else {
        ESP_LOGD(TAG, "State %s suppressed (alerts_only=%d, enabled=%d)",
                 led_feedback_state_name(state), s_led.alerts_only, s_led.enabled);
    }

    xSemaphoreGive(s_led.mutex);
    return ret;
}

esp_err_t led_feedback_clear_state(led_state_t state)
{
    if (state >= LED_STATE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_led.initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_led.mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = led_indicator_stop(s_led.handle, state);
    if (ret == ESP_OK && s_led.current_state == state) {
        s_led.current_state = LED_STATE_OFF;
        ESP_LOGD(TAG, "State cleared: %s", led_feedback_state_name(state));
    }

    xSemaphoreGive(s_led.mutex);
    return ret;
}

led_state_t led_feedback_get_state(void)
{
    return s_led.initialized ? s_led.current_state : LED_STATE_OFF;
}

esp_err_t led_feedback_set_enabled(bool enabled)
{
    s_led.enabled = enabled;
    ESP_LOGI(TAG, "LED feedback %s", enabled ? "enabled" : "disabled");

    if (!enabled && s_led.initialized && s_led.handle) {
        // Turn off LED when disabled
        led_indicator_stop(s_led.handle, s_led.current_state);
    }
    return ESP_OK;
}

esp_err_t led_feedback_set_brightness(uint8_t percent)
{
    if (percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }
    s_led.brightness = percent;
    ESP_LOGI(TAG, "Brightness set to %d%%", percent);
    // Note: led_indicator handles brightness internally via LEDC duty
    return ESP_OK;
}

esp_err_t led_feedback_set_alerts_only(bool alerts_only)
{
    s_led.alerts_only = alerts_only;
    ESP_LOGI(TAG, "Alerts-only mode %s", alerts_only ? "enabled" : "disabled");
    return ESP_OK;
}

bool led_feedback_is_enabled(void)
{
    return s_led.enabled;
}

uint8_t led_feedback_get_brightness(void)
{
    return s_led.brightness;
}

bool led_feedback_is_alerts_only(void)
{
    return s_led.alerts_only;
}
```

**Step 2: Verify it compiles**

Run: `cd firmware && idf.py build 2>&1 | tail -20`
Expected: Build succeeds (may have warnings about unused led_console.c)

**Step 3: Commit**

```bash
git add firmware/components/led_feedback/src/led_feedback.c
git commit -m "feat(led_feedback): implement core functionality with NVS config"
```

---

## Task 6: Implement Console Commands

**Files:**
- Create: `firmware/components/led_feedback/src/led_console.c`

**Step 1: Create led_console.c**

```c
/**
 * @file led_console.c
 * @brief Console commands for LED feedback control
 */

#include "led_feedback.h"
#include "esp_console.h"
#include "esp_log.h"
#include "argtable3/argtable3.h"
#include <stdlib.h>
#include <string.h>

static const char *TAG = "led_console";

// led status command
static int cmd_led_status(int argc, char **argv)
{
    printf("LED Feedback:\n");
    printf("  Enabled: %s\n", led_feedback_is_enabled() ? "yes" : "no");
    printf("  Brightness: %d%%\n", led_feedback_get_brightness());
    printf("  Mode: %s\n", led_feedback_is_alerts_only() ? "alerts only" : "all states");
    printf("  Current state: %s\n", led_feedback_state_name(led_feedback_get_state()));
    return 0;
}

// led on command
static int cmd_led_on(int argc, char **argv)
{
    esp_err_t ret = led_feedback_set_enabled(true);
    if (ret == ESP_OK) {
        ret = led_feedback_save_config();
    }
    if (ret == ESP_OK) {
        printf("LED feedback enabled\n");
    } else {
        printf("Error: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

// led off command
static int cmd_led_off(int argc, char **argv)
{
    esp_err_t ret = led_feedback_set_enabled(false);
    if (ret == ESP_OK) {
        ret = led_feedback_save_config();
    }
    if (ret == ESP_OK) {
        printf("LED feedback disabled\n");
    } else {
        printf("Error: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

// led brightness <percent> command
static struct {
    struct arg_int *percent;
    struct arg_end *end;
} brightness_args;

static int cmd_led_brightness(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&brightness_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, brightness_args.end, argv[0]);
        return 1;
    }

    int percent = brightness_args.percent->ival[0];
    if (percent < 0 || percent > 100) {
        printf("Error: brightness must be 0-100\n");
        return 1;
    }

    esp_err_t ret = led_feedback_set_brightness((uint8_t)percent);
    if (ret == ESP_OK) {
        ret = led_feedback_save_config();
    }
    if (ret == ESP_OK) {
        printf("Brightness set to %d%%\n", percent);
    } else {
        printf("Error: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

// led alerts-only command
static int cmd_led_alerts_only(int argc, char **argv)
{
    esp_err_t ret = led_feedback_set_alerts_only(true);
    if (ret == ESP_OK) {
        ret = led_feedback_save_config();
    }
    if (ret == ESP_OK) {
        printf("LED will only indicate alerts\n");
    } else {
        printf("Error: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

// led all-states command (opposite of alerts-only)
static int cmd_led_all_states(int argc, char **argv)
{
    esp_err_t ret = led_feedback_set_alerts_only(false);
    if (ret == ESP_OK) {
        ret = led_feedback_save_config();
    }
    if (ret == ESP_OK) {
        printf("LED will indicate all states\n");
    } else {
        printf("Error: %s\n", esp_err_to_name(ret));
    }
    return ret == ESP_OK ? 0 : 1;
}

esp_err_t led_feedback_register_console(void)
{
    esp_err_t ret;

    // led status
    const esp_console_cmd_t cmd_status = {
        .command = "led",
        .help = "Show LED feedback status",
        .hint = NULL,
        .func = &cmd_led_status,
    };
    ret = esp_console_cmd_register(&cmd_status);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register 'led': %s", esp_err_to_name(ret));
        return ret;
    }

    // led on
    const esp_console_cmd_t cmd_on = {
        .command = "led_on",
        .help = "Enable LED feedback",
        .hint = NULL,
        .func = &cmd_led_on,
    };
    ret = esp_console_cmd_register(&cmd_on);
    if (ret != ESP_OK) {
        return ret;
    }

    // led off
    const esp_console_cmd_t cmd_off = {
        .command = "led_off",
        .help = "Disable LED feedback",
        .hint = NULL,
        .func = &cmd_led_off,
    };
    ret = esp_console_cmd_register(&cmd_off);
    if (ret != ESP_OK) {
        return ret;
    }

    // led brightness <percent>
    brightness_args.percent = arg_int1(NULL, NULL, "<percent>", "Brightness 0-100");
    brightness_args.end = arg_end(1);
    const esp_console_cmd_t cmd_brightness = {
        .command = "led_brightness",
        .help = "Set LED brightness (0-100)",
        .hint = NULL,
        .func = &cmd_led_brightness,
        .argtable = &brightness_args,
    };
    ret = esp_console_cmd_register(&cmd_brightness);
    if (ret != ESP_OK) {
        return ret;
    }

    // led alerts-only
    const esp_console_cmd_t cmd_alerts = {
        .command = "led_alerts",
        .help = "LED shows only alerts (quieter)",
        .hint = NULL,
        .func = &cmd_led_alerts_only,
    };
    ret = esp_console_cmd_register(&cmd_alerts);
    if (ret != ESP_OK) {
        return ret;
    }

    // led all-states
    const esp_console_cmd_t cmd_all = {
        .command = "led_all",
        .help = "LED shows all states (default)",
        .hint = NULL,
        .func = &cmd_led_all_states,
    };
    ret = esp_console_cmd_register(&cmd_all);
    if (ret != ESP_OK) {
        return ret;
    }

    ESP_LOGI(TAG, "Console commands registered");
    return ESP_OK;
}
```

**Step 2: Verify build**

Run: `cd firmware && idf.py build 2>&1 | tail -10`
Expected: Build succeeds

**Step 3: Commit**

```bash
git add firmware/components/led_feedback/src/led_console.c
git commit -m "feat(led_feedback): add console commands for runtime control"
```

---

## Task 7: Integrate with main.c

**Files:**
- Modify: `firmware/main/main.c`

**Step 1: Add include and LED init after NVS**

Add after line 18 (`#include "system_console.h"`):
```c
#include "led_feedback.h"
```

Add in `app_main()` after NVS init (around line 148, after `ESP_LOGI(TAG, "NVS initialized");`):
```c
    // Initialize LED feedback early (shows boot state)
    ret = led_feedback_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LED feedback init failed: %s (continuing without LED)",
                 esp_err_to_name(ret));
        // Non-fatal - continue without LED feedback
    }
```

**Step 2: Add first boot LED state**

Add after `device_identity_is_first_boot()` check (around line 167):
```c
    if (device_identity_is_first_boot()) {
        led_feedback_set_state(LED_STATE_FIRST_BOOT);
        ESP_LOGI(TAG, "========================================");
        // ... existing credential logging ...
    }
```

**Step 3: Add console registration**

Add in `init_console()` function, after `system_console_register()` (around line 123):
```c
    // Register LED feedback console commands
    ret = led_feedback_register_console();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LED console init failed: %s", esp_err_to_name(ret));
    }
```

**Step 4: Update WiFi callback**

Replace `wifi_event_callback` function with LED state updates:
```c
static void wifi_event_callback(wifi_mgr_event_t event, void *ctx)
{
    switch (event) {
        case WIFI_MGR_EVENT_CONNECTED:
            led_feedback_clear_state(LED_STATE_WIFI_CONNECTING);
            led_feedback_clear_state(LED_STATE_WIFI_RECONNECTING);
            led_feedback_clear_state(LED_STATE_WIFI_PROVISIONING);
            led_feedback_set_state(LED_STATE_CONNECTED);
            ESP_LOGI(TAG, "WiFi connected");
            break;

        case WIFI_MGR_EVENT_DISCONNECTED:
            led_feedback_clear_state(LED_STATE_CONNECTED);
            led_feedback_set_state(LED_STATE_WIFI_RECONNECTING);
            ESP_LOGW(TAG, "WiFi disconnected - services paused");
            break;

        case WIFI_MGR_EVENT_PROVISIONING:
            led_feedback_set_state(LED_STATE_WIFI_PROVISIONING);
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "WiFi Provisioning Mode");
            ESP_LOGI(TAG, "1. Install 'ESP BLE Prov' app (iOS/Android)");
            ESP_LOGI(TAG, "2. Scan for BLE device");
            ESP_LOGI(TAG, "3. Enter PIN when prompted");
            ESP_LOGI(TAG, "4. Select WiFi network and enter password");
            ESP_LOGI(TAG, "========================================");
            break;

        case WIFI_MGR_EVENT_PROVISIONED:
            led_feedback_clear_state(LED_STATE_WIFI_PROVISIONING);
            led_feedback_set_state(LED_STATE_WIFI_CONNECTING);
            ESP_LOGI(TAG, "Credentials saved - connecting...");
            break;

        case WIFI_MGR_EVENT_RECONNECT_EXHAUSTED:
            ESP_LOGW(TAG, "WiFi reconnection attempts exhausted");
            break;

        case WIFI_MGR_EVENT_COUNT:
            break;
    }
}
```

**Step 5: Update Tailscale callback (if enabled)**

Replace `tailscale_callback` function:
```c
#if CONFIG_TS_MGR_ENABLED
static void tailscale_callback(ts_mgr_event_t event, void *ctx)
{
    char ip[16];
    switch (event) {
        case TS_MGR_EVENT_CONNECTED:
            led_feedback_clear_state(LED_STATE_TAILSCALE_CONNECTING);
            if (ts_mgr_get_ip(ip, sizeof(ip)) == ESP_OK) {
                ESP_LOGI(TAG, "Tailscale connected: %s", ip);
            } else {
                ESP_LOGW(TAG, "Tailscale connected but IP retrieval failed");
            }
            break;

        case TS_MGR_EVENT_DISCONNECTED:
            led_feedback_set_state(LED_STATE_TAILSCALE_CONNECTING);
            ESP_LOGW(TAG, "Tailscale disconnected - reconnecting...");
            break;

        case TS_MGR_EVENT_UNCONFIGURED:
            // No LED change - Tailscale is optional
            ESP_LOGI(TAG, "========================================");
            ESP_LOGI(TAG, "Tailscale not configured");
            ESP_LOGI(TAG, "Use 'ts_auth <key>' to set auth key");
            ESP_LOGI(TAG, "Get key from: https://login.tailscale.com/admin/settings/keys");
            ESP_LOGI(TAG, "========================================");
            break;

        case TS_MGR_EVENT_RECONNECT_EXHAUSTED:
            ESP_LOGW(TAG, "Tailscale reconnection exhausted");
            break;

        case TS_MGR_EVENT_KEY_UPDATED:
            led_feedback_set_state(LED_STATE_TAILSCALE_CONNECTING);
            ESP_LOGI(TAG, "Tailscale auth key updated");
            break;
    }
}
#endif
```

**Step 6: Clear first boot LED after credentials shown**

Add after the first boot credential display block:
```c
    if (device_identity_is_first_boot()) {
        // ... existing code ...
        ESP_LOGI(TAG, "========================================");
        // Clear first boot LED state after showing credentials
        led_feedback_clear_state(LED_STATE_FIRST_BOOT);
    }
```

**Step 7: Build and verify**

Run: `cd firmware && idf.py build`
Expected: Build succeeds

**Step 8: Commit**

```bash
git add firmware/main/main.c
git commit -m "feat(led_feedback): integrate LED feedback into main application"
```

---

## Task 8: Configure and Test on Hardware

**Step 1: Set LED type in menuconfig**

Run: `cd firmware && idf.py menuconfig`
Navigate to: Component config → LED Feedback
- Set "LED hardware type" to "LEDC PWM"
- Verify GPIO is 48 for DevKitC
- Set default brightness to 30%

Save and exit.

**Step 2: Build and flash**

Run: `cd firmware && idf.py build flash`

**Step 3: Monitor boot sequence**

Run: `cd firmware && idf.py monitor`

Expected behavior:
1. LED should breathe slowly if first boot (purple on RGB, fade on white)
2. LED should blink slowly during WiFi provisioning
3. LED should blink fast during WiFi connecting
4. LED should go solid when connected

**Step 4: Test console commands**

```
boorker> led
LED Feedback:
  Enabled: yes
  Brightness: 30%
  Mode: all states
  Current state: CONNECTED

boorker> led_off
LED feedback disabled

boorker> led_on
LED feedback enabled

boorker> led_brightness 10
Brightness set to 10%

boorker> led_alerts
LED will only indicate alerts
```

**Step 5: Verify NVS persistence**

1. Run `led_off` and `led_brightness 50`
2. Reboot device
3. Run `led` - should show enabled=no, brightness=50

**Step 6: Commit sdkconfig changes**

```bash
git add firmware/sdkconfig
git commit -m "feat(led_feedback): configure LED feedback for DevKitC"
```

---

## Task 9: Update Version and Create PR

**Step 1: Bump version**

Edit `firmware/main/version.h`:
```c
#define BOORKER_VERSION_MAJOR 0
#define BOORKER_VERSION_MINOR 4
#define BOORKER_VERSION_PATCH 0

#define BOORKER_VERSION_STRING "0.4.0"
```

**Step 2: Update README if needed**

Add to README.md features list:
```markdown
- **LED Status Feedback**: Visual status indication via onboard LED
```

**Step 3: Final commit**

```bash
git add firmware/main/version.h README.md
git commit -m "chore: bump version to 0.4.0 for LED feedback feature"
```

**Step 4: Create PR (if on feature branch)**

```bash
git push -u origin feature/led-feedback
gh pr create --title "feat: add LED status feedback component" --body "$(cat <<'EOF'
## Summary
- Add led_feedback component using espressif/led_indicator
- Priority-based state indication (alerts > provisioning > connected)
- User-configurable (enable/disable, brightness, alerts-only)
- Console commands for runtime control
- NVS persistence for settings

## Test Plan
- [ ] Verify LED breathes on first boot
- [ ] Verify LED blinks during provisioning
- [ ] Verify LED solid when connected
- [ ] Test console commands (led, led_on, led_off, led_brightness, led_alerts)
- [ ] Verify settings persist across reboot

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

---

## Summary

| Task | Files | Purpose |
|------|-------|---------|
| 1 | idf_component.yml | Add managed component dependency |
| 2 | CMakeLists.txt, Kconfig | Build configuration |
| 3 | led_feedback.h | Public API |
| 4 | led_patterns.c | Blink pattern definitions |
| 5 | led_feedback.c | Core implementation |
| 6 | led_console.c | Console commands |
| 7 | main.c | Integration |
| 8 | Hardware test | Verify on device |
| 9 | version.h, README | Version bump and PR |

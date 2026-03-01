# LED Feedback Component Design

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:writing-plans to create implementation plan from this design.

**Date:** 2026-02-28
**Status:** Approved
**Component:** `firmware/components/led_feedback/`

---

## Overview

A status LED feedback component for Boorker devices that provides at-a-glance system health indication without requiring the web UI. Built on Espressif's `led_indicator` component for proven pattern handling and priority management.

**Key Capabilities:**
- Priority-based state indication (higher priority states preempt lower)
- Hardware abstraction (GPIO, LEDC PWM, WS2812 RGB)
- User-configurable (enable/disable, brightness, alerts-only mode)
- Runtime config persisted to NVS
- Thread-safe for use from multiple callbacks

**Supported Hardware:**
| Board | LED Type | GPIO | Driver |
|-------|----------|------|--------|
| ESP32-S3-DevKitC-1 | WS2812 RGB | 48 | led_strip |
| Heltec WiFi LoRa 32 V3 | White LED | 35 | LEDC PWM |

---

## Architecture

### Component Structure

```
firmware/components/led_feedback/
├── CMakeLists.txt           # Build config, depends on led_indicator
├── Kconfig                  # LED type, GPIO, timing, defaults
├── include/
│   └── led_feedback.h       # Public API with thread safety docs
└── src/
    ├── led_feedback.c       # Init, state machine, NVS config
    ├── led_patterns.c       # Blink step definitions
    └── led_console.c        # Console commands (led status/off/brightness)
```

### Dependencies

```cmake
idf_component_register(
    SRCS "src/led_feedback.c" "src/led_patterns.c" "src/led_console.c"
    INCLUDE_DIRS "include"
    REQUIRES led_indicator nvs_flash esp_console
)
```

Managed component dependency:
```yaml
# idf_component.yml
dependencies:
  espressif/led_indicator: "^0.9"
```

---

## State Definitions & Priority

States defined in priority order (lowest enum value = highest priority):

```c
typedef enum {
    // Priority 0 (highest) - Immediate attention
    LED_STATE_ALERT_CRITICAL,       // Fast double-pulse

    // Priority 1 - Setup required
    LED_STATE_FIRST_BOOT,           // Slow breathe
    LED_STATE_WIFI_PROVISIONING,    // Slow blink

    // Priority 2 - Problems
    LED_STATE_WIFI_RECONNECTING,    // Medium blink
    LED_STATE_ALERT_ACTIVE,         // Double-pulse (slower)

    // Priority 3 - Transitional
    LED_STATE_WIFI_CONNECTING,      // Fast blink
    LED_STATE_TAILSCALE_CONNECTING, // Fast blink

    // Priority 4 (lowest) - Normal operation
    LED_STATE_CONNECTED,            // Solid on
    LED_STATE_OFF,                  // Off

    LED_STATE_MAX                   // Sentinel for bounds checking
} led_state_t;
```

### Conflict Resolution

The `led_indicator` component handles priority automatically:
- Higher priority state preempts current state
- When high priority state clears, lower priority resumes
- Multiple active states: highest priority wins

**Examples:**

| Active States | LED Shows | Reason |
|---------------|-----------|--------|
| WiFi connected + Tailscale connecting | Fast blink | Tailscale connecting > Connected |
| WiFi reconnecting + Alert active | Medium blink | Reconnecting > Alert |
| First boot + WiFi provisioning | Slow breathe | First boot > Provisioning |

---

## Blink Patterns

### Single-Color LED (GPIO/LEDC)

| State | Pattern | Timing | Visual |
|-------|---------|--------|--------|
| `ALERT_CRITICAL` | Double pulse | 100on, 100off, 100on, 700off | `**__**____` |
| `FIRST_BOOT` | Slow breathe | 2000ms cycle | fade in/out |
| `WIFI_PROVISIONING` | Slow blink | 1000on, 1000off | `*___*___` |
| `WIFI_RECONNECTING` | Medium blink | 500on, 500off | `*__*__` |
| `ALERT_ACTIVE` | Double pulse slow | 200on, 200off, 200on, 1400off | `**______` |
| `WIFI_CONNECTING` | Fast blink | 200on, 200off | `*_*_*_` |
| `TAILSCALE_CONNECTING` | Fast blink | 200on, 200off | `*_*_*_` |
| `CONNECTED` | Solid | Always on | `******` |
| `OFF` | Off | Always off | `______` |

### RGB LED (WS2812) Colors

| State | Color | Hex |
|-------|-------|-----|
| `ALERT_CRITICAL` | Red | `#FF0000` |
| `FIRST_BOOT` | Purple | `#8000FF` |
| `WIFI_PROVISIONING` | Blue | `#0000FF` |
| `WIFI_RECONNECTING` | Yellow | `#FFFF00` |
| `ALERT_ACTIVE` | Orange | `#FF8000` |
| `WIFI_CONNECTING` | Cyan | `#00FFFF` |
| `TAILSCALE_CONNECTING` | Cyan | `#00FFFF` |
| `CONNECTED` | Green | `#00FF00` |

### Pattern Definition Format

```c
static const blink_step_t pattern_alert_critical[] = {
    {LED_BLINK_HOLD,  LED_STATE_ON,  100},
    {LED_BLINK_HOLD,  LED_STATE_OFF, 100},
    {LED_BLINK_HOLD,  LED_STATE_ON,  100},
    {LED_BLINK_HOLD,  LED_STATE_OFF, 700},
    {LED_BLINK_LOOP,  0,             0},
};

static const blink_step_t pattern_first_boot[] = {
    {LED_BLINK_BREATHE, LED_STATE_ON,  1000},
    {LED_BLINK_BREATHE, LED_STATE_OFF, 1000},
    {LED_BLINK_LOOP,    0,             0},
};

static const blink_step_t pattern_connected[] = {
    {LED_BLINK_HOLD, LED_STATE_ON, 0},
    {LED_BLINK_STOP, 0,            0},
};
```

---

## Configuration

### Kconfig Options

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

    config LED_FEEDBACK_ACTIVE_LOW
        bool "LED is active-low"
        default n

    config LED_FEEDBACK_BLINK_FAST_MS
        int "Fast blink period (ms)"
        default 400

    config LED_FEEDBACK_BLINK_MEDIUM_MS
        int "Medium blink period (ms)"
        default 1000

    config LED_FEEDBACK_BLINK_SLOW_MS
        int "Slow blink period (ms)"
        default 2000

    config LED_FEEDBACK_BREATHE_PERIOD_MS
        int "Breathing cycle period (ms)"
        default 2000

endmenu
```

### Runtime Configuration (NVS)

```c
// Stored in NVS namespace "led_cfg"
typedef struct {
    bool enabled;        // Master on/off
    uint8_t brightness;  // 0-100
    bool alerts_only;    // Only show alerts, not normal status
} led_feedback_config_t;
```

### Console Commands

```
boorker> led status
LED Feedback: enabled
Brightness: 30%
Mode: all states
Current: CONNECTED

boorker> led off
LED feedback disabled

boorker> led on
LED feedback enabled

boorker> led brightness 10
Brightness set to 10%

boorker> led alerts-only
LED will only indicate alerts
```

---

## Public API

```c
/**
 * @file led_feedback.h
 * @brief LED status feedback component
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

typedef enum {
    LED_STATE_ALERT_CRITICAL,
    LED_STATE_FIRST_BOOT,
    LED_STATE_WIFI_PROVISIONING,
    LED_STATE_WIFI_RECONNECTING,
    LED_STATE_ALERT_ACTIVE,
    LED_STATE_WIFI_CONNECTING,
    LED_STATE_TAILSCALE_CONNECTING,
    LED_STATE_CONNECTED,
    LED_STATE_OFF,
    LED_STATE_MAX
} led_state_t;

// Lifecycle
esp_err_t led_feedback_init(void);
esp_err_t led_feedback_deinit(void);

// State control
esp_err_t led_feedback_set_state(led_state_t state);
esp_err_t led_feedback_clear_state(led_state_t state);
led_state_t led_feedback_get_state(void);

// Runtime configuration
esp_err_t led_feedback_set_enabled(bool enabled);
esp_err_t led_feedback_set_brightness(uint8_t percent);
esp_err_t led_feedback_set_alerts_only(bool alerts_only);
esp_err_t led_feedback_save_config(void);

// Queries
bool led_feedback_is_enabled(void);
uint8_t led_feedback_get_brightness(void);

// Console registration
esp_err_t led_feedback_register_console(void);
```

---

## Integration

### main.c Integration

```c
#include "led_feedback.h"

static void wifi_event_callback(wifi_mgr_event_t event, void *ctx)
{
    switch (event) {
        case WIFI_MGR_EVENT_CONNECTED:
            led_feedback_clear_state(LED_STATE_WIFI_CONNECTING);
            led_feedback_clear_state(LED_STATE_WIFI_RECONNECTING);
            led_feedback_set_state(LED_STATE_CONNECTED);
            break;

        case WIFI_MGR_EVENT_DISCONNECTED:
            led_feedback_clear_state(LED_STATE_CONNECTED);
            led_feedback_set_state(LED_STATE_WIFI_RECONNECTING);
            break;

        case WIFI_MGR_EVENT_PROVISIONING:
            led_feedback_set_state(LED_STATE_WIFI_PROVISIONING);
            break;

        case WIFI_MGR_EVENT_PROVISIONED:
            led_feedback_clear_state(LED_STATE_WIFI_PROVISIONING);
            led_feedback_set_state(LED_STATE_WIFI_CONNECTING);
            break;

        case WIFI_MGR_EVENT_RECONNECT_EXHAUSTED:
        case WIFI_MGR_EVENT_COUNT:
            break;
    }
}

static void tailscale_callback(ts_mgr_event_t event, void *ctx)
{
    switch (event) {
        case TS_MGR_EVENT_CONNECTED:
            led_feedback_clear_state(LED_STATE_TAILSCALE_CONNECTING);
            break;

        case TS_MGR_EVENT_DISCONNECTED:
            led_feedback_set_state(LED_STATE_TAILSCALE_CONNECTING);
            break;

        case TS_MGR_EVENT_UNCONFIGURED:
        case TS_MGR_EVENT_RECONNECT_EXHAUSTED:
        case TS_MGR_EVENT_KEY_UPDATED:
            break;
    }
}

void app_main(void)
{
    // ... NVS init ...

    // Initialize LED feedback early
    esp_err_t ret = led_feedback_init();
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "LED feedback init failed: %s", esp_err_to_name(ret));
        // Non-fatal - continue without LED feedback
    }

    // Check first boot
    if (device_identity_is_first_boot()) {
        led_feedback_set_state(LED_STATE_FIRST_BOOT);
    }

    // ... console init ...
    led_feedback_register_console();

    // ... WiFi init with callbacks ...
}
```

---

## Defensive Coding

### Thread Safety

```c
static SemaphoreHandle_t s_led_mutex = NULL;

esp_err_t led_feedback_set_state(led_state_t state)
{
    if (state >= LED_STATE_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    if (xSemaphoreTake(s_led_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    esp_err_t ret = led_indicator_start(s_handle, state);

    xSemaphoreGive(s_led_mutex);
    return ret;
}
```

### Init Validation

```c
esp_err_t led_feedback_init(void)
{
    if (s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    s_led_mutex = xSemaphoreCreateMutex();
    if (s_led_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    // Load NVS config with defaults fallback
    esp_err_t ret = load_config_from_nvs();
    if (ret != ESP_OK && ret != ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGW(TAG, "NVS load failed: %s", esp_err_to_name(ret));
    }

    // Create indicator
    s_handle = led_indicator_create(&config);
    if (s_handle == NULL) {
        vSemaphoreDelete(s_led_mutex);
        return ESP_FAIL;
    }

    s_initialized = true;
    return ESP_OK;
}
```

### Internal State Structure

```c
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
```

---

## File Estimates

| File | Lines | Purpose |
|------|-------|---------|
| `CMakeLists.txt` | ~15 | Build config |
| `Kconfig` | ~60 | Configuration options |
| `led_feedback.h` | ~80 | Public API |
| `led_feedback.c` | ~250 | Core implementation |
| `led_patterns.c` | ~100 | Pattern definitions |
| `led_console.c` | ~80 | Console commands |
| **Total** | ~585 | |

---

## Out of Scope (YAGNI)

- External LED strip support (future extension)
- Sensor-specific LED plugins (future extension)
- Web API endpoint (add when web UI supports it)
- Activity indication (TX/RX flashes)

---

## References

- [Espressif LED Indicator Component](https://docs.espressif.com/projects/esp-iot-solution/en/latest/display/led_indicator.html)
- [ESP-IDF LEDC API](https://docs.espressif.com/projects/esp-idf/en/latest/esp32/api-reference/peripherals/ledc.html)
- [ESP32-S3-DevKitC-1 RGB LED](https://www.phippselectronics.com/esp32-s3-devkitc-1-blink-onboard-rgb-led/)

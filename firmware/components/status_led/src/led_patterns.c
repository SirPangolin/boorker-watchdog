/**
 * @file led_patterns.c
 * @brief Blink pattern definitions for status LED states
 */

#include "led_indicator.h"
#include "status_led.h"
#include "sdkconfig.h"

// Include RGB colors for WS2812 (onboard) or RGB_LEDC (external 3-channel)
// Note: Uses led_driver Kconfig names (CONFIG_LED_DRIVER_*)
#if CONFIG_LED_DRIVER_ONBOARD_WS2812 || CONFIG_LED_DRIVER_EXTERNAL_RGB_LEDC
#include "led_convert.h"

// RGB colors (from design doc)
#define COLOR_RED        SET_RGB(255, 0, 0)      // Critical alert
#define COLOR_PURPLE     SET_RGB(128, 0, 255)    // First boot
#define COLOR_BLUE       SET_RGB(0, 0, 255)      // WiFi provisioning
#define COLOR_YELLOW     SET_RGB(255, 255, 0)    // WiFi reconnecting
#define COLOR_ORANGE     SET_RGB(255, 128, 0)    // Alert active
#define COLOR_CYAN       SET_RGB(0, 255, 255)    // Connecting states
#define COLOR_GREEN      SET_RGB(0, 255, 0)      // Connected
#define COLOR_OFF        SET_RGB(0, 0, 0)        // Off

// Macro to check if any RGB mode is active
#define LED_HAS_RGB_SUPPORT 1
#endif

// Helper macros for timing from Kconfig
#define FAST_ON     (CONFIG_STATUS_LED_BLINK_FAST_MS / 2)
#define FAST_OFF    (CONFIG_STATUS_LED_BLINK_FAST_MS / 2)
#define MEDIUM_ON   (CONFIG_STATUS_LED_BLINK_MEDIUM_MS / 2)
#define MEDIUM_OFF  (CONFIG_STATUS_LED_BLINK_MEDIUM_MS / 2)
#define SLOW_ON     (CONFIG_STATUS_LED_BLINK_SLOW_MS / 2)
#define SLOW_OFF    (CONFIG_STATUS_LED_BLINK_SLOW_MS / 2)
#define BREATHE_IN  (CONFIG_STATUS_LED_BREATHE_PERIOD_MS / 2)
#define BREATHE_OUT (CONFIG_STATUS_LED_BREATHE_PERIOD_MS / 2)

// Pattern: Critical alert - fast double pulse (red)
static const blink_step_t pattern_alert_critical[] = {
#ifdef LED_HAS_RGB_SUPPORT
    {LED_BLINK_RGB,   COLOR_RED,     0},
#endif
    {LED_BLINK_HOLD,  LED_STATE_ON,  100},
    {LED_BLINK_HOLD,  LED_STATE_OFF, 100},
    {LED_BLINK_HOLD,  LED_STATE_ON,  100},
    {LED_BLINK_HOLD,  LED_STATE_OFF, 700},
    {LED_BLINK_LOOP,  0,             0},
};

// Pattern: First boot - slow breathe (purple)
static const blink_step_t pattern_first_boot[] = {
#ifdef LED_HAS_RGB_SUPPORT
    {LED_BLINK_RGB,     COLOR_PURPLE,  0},
#endif
    {LED_BLINK_BREATHE, LED_STATE_ON,  BREATHE_IN},
    {LED_BLINK_BREATHE, LED_STATE_OFF, BREATHE_OUT},
    {LED_BLINK_LOOP,    0,             0},
};

// Pattern: WiFi provisioning - slow blink (blue)
static const blink_step_t pattern_wifi_provisioning[] = {
#ifdef LED_HAS_RGB_SUPPORT
    {LED_BLINK_RGB, COLOR_BLUE,    0},
#endif
    {LED_BLINK_HOLD, LED_STATE_ON,  SLOW_ON},
    {LED_BLINK_HOLD, LED_STATE_OFF, SLOW_OFF},
    {LED_BLINK_LOOP, 0,             0},
};

// Pattern: WiFi reconnecting - medium blink (yellow)
static const blink_step_t pattern_wifi_reconnecting[] = {
#ifdef LED_HAS_RGB_SUPPORT
    {LED_BLINK_RGB, COLOR_YELLOW,  0},
#endif
    {LED_BLINK_HOLD, LED_STATE_ON,  MEDIUM_ON},
    {LED_BLINK_HOLD, LED_STATE_OFF, MEDIUM_OFF},
    {LED_BLINK_LOOP, 0,             0},
};

// Pattern: Alert active - slow double pulse (orange)
static const blink_step_t pattern_alert_active[] = {
#ifdef LED_HAS_RGB_SUPPORT
    {LED_BLINK_RGB,   COLOR_ORANGE,  0},
#endif
    {LED_BLINK_HOLD,  LED_STATE_ON,  200},
    {LED_BLINK_HOLD,  LED_STATE_OFF, 200},
    {LED_BLINK_HOLD,  LED_STATE_ON,  200},
    {LED_BLINK_HOLD,  LED_STATE_OFF, 1400},
    {LED_BLINK_LOOP,  0,             0},
};

// Pattern: WiFi connecting - fast blink (cyan)
static const blink_step_t pattern_wifi_connecting[] = {
#ifdef LED_HAS_RGB_SUPPORT
    {LED_BLINK_RGB, COLOR_CYAN,    0},
#endif
    {LED_BLINK_HOLD, LED_STATE_ON,  FAST_ON},
    {LED_BLINK_HOLD, LED_STATE_OFF, FAST_OFF},
    {LED_BLINK_LOOP, 0,             0},
};

// Pattern: Tailscale connecting - fast blink (cyan)
static const blink_step_t pattern_tailscale_connecting[] = {
#ifdef LED_HAS_RGB_SUPPORT
    {LED_BLINK_RGB, COLOR_CYAN,    0},
#endif
    {LED_BLINK_HOLD, LED_STATE_ON,  FAST_ON},
    {LED_BLINK_HOLD, LED_STATE_OFF, FAST_OFF},
    {LED_BLINK_LOOP, 0,             0},
};

// Pattern: Connected - solid on (green)
static const blink_step_t pattern_connected[] = {
#ifdef LED_HAS_RGB_SUPPORT
    {LED_BLINK_RGB, COLOR_GREEN,   0},
#endif
    {LED_BLINK_HOLD, LED_STATE_ON, 0},
    {LED_BLINK_STOP, 0,            0},
};

// Pattern: Off - solid off
static const blink_step_t pattern_off[] = {
#ifdef LED_HAS_RGB_SUPPORT
    {LED_BLINK_RGB, COLOR_OFF,     0},
#endif
    {LED_BLINK_HOLD, LED_STATE_OFF, 0},
    {LED_BLINK_STOP, 0,             0},
};

// Pattern list indexed by status_led_state_t
// MUST match status_led_state_t enum order exactly
blink_step_t const *led_patterns[] = {
    [STATUS_LED_ALERT_CRITICAL]       = pattern_alert_critical,
    [STATUS_LED_FIRST_BOOT]           = pattern_first_boot,
    [STATUS_LED_WIFI_PROVISIONING]    = pattern_wifi_provisioning,
    [STATUS_LED_WIFI_RECONNECTING]    = pattern_wifi_reconnecting,
    [STATUS_LED_ALERT_ACTIVE]         = pattern_alert_active,
    [STATUS_LED_WIFI_CONNECTING]      = pattern_wifi_connecting,
    [STATUS_LED_TAILSCALE_CONNECTING] = pattern_tailscale_connecting,
    [STATUS_LED_CONNECTED]            = pattern_connected,
    [STATUS_LED_OFF]                  = pattern_off,
};

// Verify array size matches enum at compile time
_Static_assert(sizeof(led_patterns) / sizeof(led_patterns[0]) == STATUS_LED_MAX,
               "led_patterns array size must match STATUS_LED_MAX");

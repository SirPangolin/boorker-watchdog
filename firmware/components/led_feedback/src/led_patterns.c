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
    [LED_FB_ALERT_CRITICAL]       = pattern_alert_critical,
    [LED_FB_FIRST_BOOT]           = pattern_first_boot,
    [LED_FB_WIFI_PROVISIONING]    = pattern_wifi_provisioning,
    [LED_FB_WIFI_RECONNECTING]    = pattern_wifi_reconnecting,
    [LED_FB_ALERT_ACTIVE]         = pattern_alert_active,
    [LED_FB_WIFI_CONNECTING]      = pattern_wifi_connecting,
    [LED_FB_TAILSCALE_CONNECTING] = pattern_tailscale_connecting,
    [LED_FB_CONNECTED]            = pattern_connected,
    [LED_FB_OFF]                  = pattern_off,
};

// Verify array size matches enum at compile time
_Static_assert(sizeof(led_patterns) / sizeof(led_patterns[0]) == LED_FB_MAX,
               "led_patterns array size must match LED_FB_MAX");

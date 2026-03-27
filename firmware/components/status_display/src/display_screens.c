/**
 * @file display_screens.c
 * @brief Screen rendering functions using u8g2
 *
 * All screens render into the u8g2 buffer. Caller is responsible
 * for calling u8g2_SendBuffer() after rendering.
 *
 * Display: 128x64 monochrome SSD1306 (blue pixels on black).
 * Layout: ~12px header, ~40px content, ~12px footer.
 */

#include "sdkconfig.h"

#if CONFIG_STATUS_DISPLAY_ENABLED

#include <math.h>
#include "u8g2.h"
#include "sensor_manager.h"
#include "sensor_types.h"
#include "credentials.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "version.h"

#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "display_internal.h"

// --------------------------------------------------------------------------
// Shared helpers
// --------------------------------------------------------------------------

static void draw_header(u8g2_t *u8g2, const char *title, const char *right_text)
{
    u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
    u8g2_DrawStr(u8g2, 0, 7, title);
    if (right_text) {
        int w = u8g2_GetStrWidth(u8g2, right_text);
        u8g2_DrawStr(u8g2, 128 - w, 7, right_text);
    }
    u8g2_DrawHLine(u8g2, 0, 9, 128);
}

/**
 * @brief Draw footer with dot navigation
 *
 * @param current_page 0-based index of current screen in the full cycle
 *        0=dashboard, 1=network, 2=lora, 3=system, 4=nodes, 5=sensors
 * @param total_pages Total number of pages in the cycle (typically 6)
 */
static void draw_footer_dots(u8g2_t *u8g2, int current_page, int total_pages)
{
    u8g2_DrawHLine(u8g2, 0, 52, 128);

    // Draw dot indicators on the left
    int dot_spacing = 8;
    int dot_x = 2;
    int dot_y = 58;

    for (int i = 0; i < total_pages && i < 8; i++) {
        if (i == current_page) {
            u8g2_DrawDisc(u8g2, dot_x + i * dot_spacing, dot_y, 2, U8G2_DRAW_ALL);
        } else {
            u8g2_DrawCircle(u8g2, dot_x + i * dot_spacing, dot_y, 2, U8G2_DRAW_ALL);
        }
    }

    // PRG hint on the right
    u8g2_SetFont(u8g2, u8g2_font_tom_thumb_4x6_tf);
    const char *hint = "PRG:next";
    int w = u8g2_GetStrWidth(u8g2, hint);
    u8g2_DrawStr(u8g2, 128 - w, 62, hint);
}

// Navigation dot indices — total count of navigable screens
#define NAV_TOTAL  6

/**
 * @brief Map a screen rendering function to its dot navigation index
 *
 * Centralized mapping prevents SCREEN_IDX_* constants from drifting
 * out of sync with screen_state_t in status_display.c.
 * Callers pass __func__ or a screen name string; this is simpler
 * than importing the private screen_state_t enum.
 */
#define NAV_IDX_DASHBOARD  0
#define NAV_IDX_NETWORK    1
#define NAV_IDX_LORA       2
#define NAV_IDX_SYSTEM     3
#define NAV_IDX_NODES      4
#define NAV_IDX_SENSORS    5

// --------------------------------------------------------------------------
// 1. Boot Splash
// --------------------------------------------------------------------------

void screen_splash(u8g2_t *u8g2, int throbber_phase)
{
    // Side-by-side brand lockup: logo left, text right

    // Millie logo — left side, vertically centered
    int logo_x = 2;
    int logo_y = (64 - millie_logo_height) / 2;
    u8g2_DrawXBM(u8g2, logo_x, logo_y, millie_logo_width, millie_logo_height, millie_logo_xbm);

    // Right-side text area — centered within available space
    int area_x = logo_x + millie_logo_width + 2;
    int area_w = 128 - area_x;

    // "BOORKER" — bold, centered
    u8g2_SetFont(u8g2, u8g2_font_7x14B_tf);
    const char *brand = "BOORKER";
    int bw = u8g2_GetStrWidth(u8g2, brand);
    u8g2_DrawStr(u8g2, area_x + (area_w - bw) / 2, 30, brand);

    // "WATCHDOG" — smaller, centered below
    u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
    const char *sub = "WATCHDOG";
    int sw = u8g2_GetStrWidth(u8g2, sub);
    u8g2_DrawStr(u8g2, area_x + (area_w - sw) / 2, 42, sub);

    // Throbber dots — centered on full display width
    int dot_span = 2 * 8;  // 3 dots at 8px spacing
    int dot_x0 = (128 - dot_span) / 2;
    for (int i = 0; i < 3; i++) {
        int x = dot_x0 + i * 8;
        if (i == throbber_phase) {
            u8g2_DrawDisc(u8g2, x, 55, 2, U8G2_DRAW_ALL);
        } else {
            u8g2_DrawCircle(u8g2, x, 55, 2, U8G2_DRAW_ALL);
        }
    }
}

// --------------------------------------------------------------------------
// 2. First Boot
// --------------------------------------------------------------------------

void screen_first_boot(u8g2_t *u8g2, const credentials_t *creds, const char *ip_str)
{
    if (!creds) return;

    // TODO: QR code rendering — for now show text placeholder
    // Left side: QR placeholder
    u8g2_SetFont(u8g2, u8g2_font_tom_thumb_4x6_tf);
    u8g2_DrawFrame(u8g2, 2, 2, 40, 40);
    u8g2_DrawStr(u8g2, 8, 24, "QR");

    // Right side: credentials
    int x = 46;
    u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
    u8g2_DrawStr(u8g2, x, 8, creds->node_name);

    u8g2_SetFont(u8g2, u8g2_font_tom_thumb_4x6_tf);
    u8g2_DrawStr(u8g2, x, 16, "CLAIM ME");

    char buf[48];
    u8g2_SetFont(u8g2, u8g2_font_tom_thumb_4x6_tf);

    snprintf(buf, sizeof(buf), "Web:%.12s", creds->web_password);
    u8g2_DrawStr(u8g2, x, 26, buf);

    snprintf(buf, sizeof(buf), "PIN:%s", creds->ble_pop);
    u8g2_DrawStr(u8g2, x, 34, buf);

    snprintf(buf, sizeof(buf), "AP:%.12s", creds->ap_password);
    u8g2_DrawStr(u8g2, x, 42, buf);

    snprintf(buf, sizeof(buf), "IP:%s", ip_str ? ip_str : "--");
    u8g2_DrawStr(u8g2, x, 50, buf);

    // Provisioning hint
    u8g2_DrawStr(u8g2, 0, 62, "Scan QR w/ ESP BLE Prov app");
}

// --------------------------------------------------------------------------
// 3. Dashboard — metric card (one sensor value per screen, auto-rotates)
// --------------------------------------------------------------------------

/**
 * @brief Render a centered metric card (shared by primary and secondary values)
 */
static void draw_metric_card(u8g2_t *u8g2, const char *label, const char *value_str)
{
    u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
    int lw = u8g2_GetStrWidth(u8g2, label);
    u8g2_DrawStr(u8g2, (128 - lw) / 2, 22, label);

    u8g2_SetFont(u8g2, u8g2_font_logisoso20_tf);
    int vw = u8g2_GetStrWidth(u8g2, value_str);
    u8g2_DrawStr(u8g2, (128 - vw) / 2, 46, value_str);

    draw_footer_dots(u8g2, NAV_IDX_DASHBOARD, NAV_TOTAL);
}

/**
 * @brief Resolve a metric_index to its label and value string
 *
 * Walks all sensors, flattening primary (value) and secondary (value2) into
 * a linear metric list. Both screen_dashboard_card and screen_get_metric_count
 * use this to ensure consistent counting.
 *
 * @param metric_index Which metric to resolve (0-based)
 * @param label Output label buffer (caller provides, min 20 chars)
 * @param label_len Size of label buffer
 * @param value_str Output value buffer (caller provides, min 20 chars)
 * @param value_len Size of value buffer
 * @return true if metric_index was found and buffers populated
 */
static bool resolve_metric(int metric_index, char *label, size_t label_len,
                           char *value_str, size_t value_len)
{
    int metric_count = 0;
    size_t sensor_count = sensor_manager_get_sensor_count();

    for (size_t s = 0; s < sensor_count; s++) {
        sensor_reading_t reading;
        const char *id = sensor_manager_get_sensor_id(s);
        if (sensor_manager_get_reading_by_index(s, &reading) != ESP_OK) continue;

        // Primary value
        if (metric_index == metric_count) {
            if (!isnan(reading.value)) {
                if (id && strstr(id, "temp")) {
                    snprintf(label, label_len, "TEMPERATURE");
                    snprintf(value_str, value_len, "%.1f F", reading.value);
                } else {
                    snprintf(label, label_len, "%s", id ? id : "SENSOR");
                    for (char *p = label; *p; p++) *p = toupper((unsigned char)*p);
                    snprintf(value_str, value_len, "%.1f", reading.value);
                }
            } else {
                snprintf(label, label_len, "%s", id ? id : "SENSOR");
                for (char *p = label; *p; p++) *p = toupper((unsigned char)*p);
                snprintf(value_str, value_len, "%s", sensor_status_name(reading.status));
            }
            return true;
        }
        metric_count++;

        // Secondary value (e.g., humidity from DHT22)
        if (!isnan(reading.value2)) {
            if (metric_index == metric_count) {
                if (id && strstr(id, "temp")) {
                    snprintf(label, label_len, "HUMIDITY");
                    snprintf(value_str, value_len, "%.0f%%", reading.value2);
                } else {
                    snprintf(label, label_len, "%s (2)", id ? id : "SENSOR");
                    snprintf(value_str, value_len, "%.1f", reading.value2);
                }
                return true;
            }
            metric_count++;
        }
    }

    return false;
}

void screen_dashboard_card(u8g2_t *u8g2, int metric_index)
{
    draw_header(u8g2, "BOORKER", NULL);

    char label[20];
    char value_str[20];

    if (resolve_metric(metric_index, label, sizeof(label), value_str, sizeof(value_str))) {
        draw_metric_card(u8g2, label, value_str);
    } else {
        u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
        u8g2_DrawStr(u8g2, 10, 32, "No sensors");
        u8g2_DrawStr(u8g2, 10, 44, "configured");
        draw_footer_dots(u8g2, NAV_IDX_DASHBOARD, NAV_TOTAL);
    }
}

/**
 * @brief Get total number of metrics available for dashboard rotation
 *
 * Uses the same walk as resolve_metric to ensure consistent counting.
 */
int screen_get_metric_count(void)
{
    int count = 0;
    size_t sensor_count = sensor_manager_get_sensor_count();
    for (size_t s = 0; s < sensor_count; s++) {
        sensor_reading_t reading;
        if (sensor_manager_get_reading_by_index(s, &reading) != ESP_OK) continue;
        count++;
        if (!isnan(reading.value2)) {
            count++;
        }
    }
    return count;
}

// --------------------------------------------------------------------------
// 4. Alert Override
// --------------------------------------------------------------------------

void screen_alert(u8g2_t *u8g2, const char *source, const char *message, bool silenced)
{
    draw_header(u8g2, "!! ALERT !!", "CRITICAL");

    u8g2_SetFont(u8g2, u8g2_font_tom_thumb_4x6_tf);
    if (source) {
        int sw = u8g2_GetStrWidth(u8g2, source);
        u8g2_DrawStr(u8g2, (128 - sw) / 2, 20, source);
    }

    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    if (message) {
        int mw = u8g2_GetStrWidth(u8g2, message);
        u8g2_DrawStr(u8g2, (128 - mw) / 2, 36, message);
    }

    // Alert footer — no dots, just action hints
    u8g2_DrawHLine(u8g2, 0, 52, 128);
    u8g2_SetFont(u8g2, u8g2_font_tom_thumb_4x6_tf);
    u8g2_DrawStr(u8g2, 0, 62, silenced ? "PRG:ack" : "PRG:silence");
}

// --------------------------------------------------------------------------
// 5. Drill-down screens
// --------------------------------------------------------------------------

void screen_network(u8g2_t *u8g2)
{
    draw_header(u8g2, "NETWORK", NULL);

    wifi_ap_record_t ap;
    char buf[48];
    u8g2_SetFont(u8g2, u8g2_font_tom_thumb_4x6_tf);
    int y = 18;

    if (esp_wifi_sta_get_ap_info(&ap) == ESP_OK) {
        snprintf(buf, sizeof(buf), "SSID:%s", (char *)ap.ssid);
        u8g2_DrawStr(u8g2, 0, y, buf); y += 8;
        snprintf(buf, sizeof(buf), "RSSI:%d dBm", ap.rssi);
        u8g2_DrawStr(u8g2, 0, y, buf); y += 8;
    } else {
        u8g2_DrawStr(u8g2, 0, y, "WiFi: not connected"); y += 8;
    }

    esp_netif_ip_info_t ip_info;
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif && esp_netif_get_ip_info(netif, &ip_info) == ESP_OK) {
        snprintf(buf, sizeof(buf), "IP:" IPSTR, IP2STR(&ip_info.ip));
        u8g2_DrawStr(u8g2, 0, y, buf); y += 8;
    }

    uint8_t mac[6];
    if (esp_wifi_get_mac(WIFI_IF_STA, mac) == ESP_OK) {
        snprintf(buf, sizeof(buf), "MAC:%02X:%02X:%02X:%02X:%02X:%02X",
                 mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
        u8g2_DrawStr(u8g2, 0, y, buf);
    }

    draw_footer_dots(u8g2, NAV_IDX_NETWORK, NAV_TOTAL);
}

void screen_lora(u8g2_t *u8g2)
{
    draw_header(u8g2, "LORA", NULL);

    u8g2_SetFont(u8g2, u8g2_font_tom_thumb_4x6_tf);
    int y = 18;
    u8g2_DrawStr(u8g2, 0, y, "Freq: 915 MHz"); y += 8;
    u8g2_DrawStr(u8g2, 0, y, "TX Pwr: +22 dBm"); y += 8;
    u8g2_DrawStr(u8g2, 0, y, "Status: Not impl"); y += 8;
    u8g2_DrawStr(u8g2, 0, y, "Peers: --");

    draw_footer_dots(u8g2, NAV_IDX_LORA, NAV_TOTAL);
}

void screen_system(u8g2_t *u8g2)
{
    draw_header(u8g2, "SYSTEM", NULL);

    char buf[48];
    u8g2_SetFont(u8g2, u8g2_font_tom_thumb_4x6_tf);
    int y = 18;

    snprintf(buf, sizeof(buf), "FW: v%s", BOORKER_VERSION_STRING);
    u8g2_DrawStr(u8g2, 0, y, buf); y += 8;

    esp_chip_info_t chip;
    esp_chip_info(&chip);
    snprintf(buf, sizeof(buf), "Chip: ESP32-S3 r%d.%d", chip.revision / 100, chip.revision % 100);
    u8g2_DrawStr(u8g2, 0, y, buf); y += 8;

    snprintf(buf, sizeof(buf), "Heap: %lu KB", esp_get_free_heap_size() / 1024);
    u8g2_DrawStr(u8g2, 0, y, buf); y += 8;

    int64_t uptime_s = esp_timer_get_time() / 1000000;
    int days = uptime_s / 86400;
    int hours = (uptime_s % 86400) / 3600;
    int mins = (uptime_s % 3600) / 60;
    snprintf(buf, sizeof(buf), "Up: %dd %dh %dm", days, hours, mins);
    u8g2_DrawStr(u8g2, 0, y, buf);

    draw_footer_dots(u8g2, NAV_IDX_SYSTEM, NAV_TOTAL);
}

void screen_nodes(u8g2_t *u8g2)
{
    draw_header(u8g2, "NODES", NULL);

    u8g2_SetFont(u8g2, u8g2_font_tom_thumb_4x6_tf);
    // For now, only local node
    const credentials_t *creds = credentials_get();
    if (creds) {
        u8g2_DrawStr(u8g2, 0, 18, creds->node_name);
        u8g2_DrawStr(u8g2, 90, 18, "LOCAL");
    }
    u8g2_DrawStr(u8g2, 0, 30, "Mesh: not implemented");

    draw_footer_dots(u8g2, NAV_IDX_NODES, NAV_TOTAL);
}

void screen_sensors(u8g2_t *u8g2)
{
    draw_header(u8g2, "SENSORS", NULL);

    u8g2_SetFont(u8g2, u8g2_font_tom_thumb_4x6_tf);
    int y = 18;

    size_t count = sensor_manager_get_sensor_count();
    for (size_t i = 0; i < count && i < 5; i++) {
        sensor_reading_t reading;
        const char *id = sensor_manager_get_sensor_id(i);
        if (sensor_manager_get_reading_by_index(i, &reading) == ESP_OK) {
            char buf[48];
            if (!isnan(reading.value)) {
                snprintf(buf, sizeof(buf), "%s: %.1f", id ? id : "?", reading.value);
            } else {
                snprintf(buf, sizeof(buf), "%s: %s", id ? id : "?", sensor_status_name(reading.status));
            }
            u8g2_DrawStr(u8g2, 0, y, buf);
            y += 8;
        }
    }

    if (count == 0) {
        u8g2_DrawStr(u8g2, 0, y, "No sensors configured");
    }

    draw_footer_dots(u8g2, NAV_IDX_SENSORS, NAV_TOTAL);
}

#endif /* CONFIG_STATUS_DISPLAY_ENABLED */

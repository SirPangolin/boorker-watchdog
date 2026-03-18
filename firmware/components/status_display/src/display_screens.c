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

#include <string.h>
#include <stdio.h>

// External logo data (from display_logo.c)
extern const uint8_t millie_logo_xbm[];
extern const int millie_logo_width;
extern const int millie_logo_height;

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

// Screen index constants for dot navigation
#define SCREEN_IDX_DASHBOARD  0
#define SCREEN_IDX_NETWORK    1
#define SCREEN_IDX_LORA       2
#define SCREEN_IDX_SYSTEM     3
#define SCREEN_IDX_NODES      4
#define SCREEN_IDX_SENSORS    5
#define SCREEN_IDX_TOTAL      6

// --------------------------------------------------------------------------
// 1. Boot Splash
// --------------------------------------------------------------------------

void screen_splash(u8g2_t *u8g2, int throbber_phase)
{
    // Draw Millie logo centered
    int logo_x = (128 - millie_logo_width) / 2;
    u8g2_DrawXBM(u8g2, logo_x, 2, millie_logo_width, millie_logo_height, millie_logo_xbm);

    // Brand text below logo
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    const char *brand = "BOORKER";
    int bw = u8g2_GetStrWidth(u8g2, brand);
    u8g2_DrawStr(u8g2, (128 - bw) / 2, 46, brand);

    u8g2_SetFont(u8g2, u8g2_font_tom_thumb_4x6_tf);
    const char *sub = "WATCHDOG";
    int sw = u8g2_GetStrWidth(u8g2, sub);
    u8g2_DrawStr(u8g2, (128 - sw) / 2, 52, sub);

    // Throbber dots
    u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
    for (int i = 0; i < 3; i++) {
        int x = 56 + i * 8;
        if (i == throbber_phase) {
            u8g2_DrawDisc(u8g2, x, 59, 2, U8G2_DRAW_ALL);
        } else {
            u8g2_DrawCircle(u8g2, x, 59, 2, U8G2_DRAW_ALL);
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

void screen_dashboard_card(u8g2_t *u8g2, int metric_index)
{
    draw_header(u8g2, "BOORKER", NULL);

    size_t sensor_count = sensor_manager_get_sensor_count();

    if (sensor_count == 0 || metric_index < 0) {
        u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
        u8g2_DrawStr(u8g2, 10, 32, "No sensors");
        u8g2_DrawStr(u8g2, 10, 44, "configured");
        draw_footer_dots(u8g2, SCREEN_IDX_DASHBOARD, SCREEN_IDX_TOTAL);
        return;
    }

    // Map metric_index to sensor + value
    // DHT22 produces 2 metrics (temp, humidity), SW-420 produces 1 (vibration status)
    // Walk through sensors, counting metrics
    int metric_count = 0;
    for (size_t s = 0; s < sensor_count; s++) {
        sensor_reading_t reading;
        const char *id = sensor_manager_get_sensor_id(s);
        if (sensor_manager_get_reading_by_index(s, &reading) != ESP_OK) continue;

        // Primary value (value)
        if (metric_index == metric_count) {
            char label[20];
            char value_str[20];

            if (!isnan(reading.value)) {
                // Determine label from sensor ID
                if (id && strstr(id, "temp")) {
                    snprintf(label, sizeof(label), "TEMPERATURE");
                    snprintf(value_str, sizeof(value_str), "%.1f F", reading.value);
                } else {
                    snprintf(label, sizeof(label), "%s", id ? id : "SENSOR");
                    snprintf(value_str, sizeof(value_str), "%.1f", reading.value);
                }
            } else {
                snprintf(label, sizeof(label), "%s", id ? id : "SENSOR");
                snprintf(value_str, sizeof(value_str), "%s", sensor_status_name(reading.status));
            }

            // Render metric card — label small at top, value big in center
            u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
            int lw = u8g2_GetStrWidth(u8g2, label);
            u8g2_DrawStr(u8g2, (128 - lw) / 2, 22, label);

            u8g2_SetFont(u8g2, u8g2_font_logisoso20_tf);
            int vw = u8g2_GetStrWidth(u8g2, value_str);
            u8g2_DrawStr(u8g2, (128 - vw) / 2, 46, value_str);

            draw_footer_dots(u8g2, SCREEN_IDX_DASHBOARD, SCREEN_IDX_TOTAL);
            return;
        }
        metric_count++;

        // Secondary value (value2) — e.g., humidity from DHT22
        if (!isnan(reading.value2)) {
            if (metric_index == metric_count) {
                char label[20];
                char value_str[20];

                if (id && strstr(id, "temp")) {
                    snprintf(label, sizeof(label), "HUMIDITY");
                    snprintf(value_str, sizeof(value_str), "%.0f%%", reading.value2);
                } else {
                    snprintf(label, sizeof(label), "%s (2)", id ? id : "SENSOR");
                    snprintf(value_str, sizeof(value_str), "%.1f", reading.value2);
                }

                u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
                int lw = u8g2_GetStrWidth(u8g2, label);
                u8g2_DrawStr(u8g2, (128 - lw) / 2, 22, label);

                u8g2_SetFont(u8g2, u8g2_font_logisoso20_tf);
                int vw = u8g2_GetStrWidth(u8g2, value_str);
                u8g2_DrawStr(u8g2, (128 - vw) / 2, 46, value_str);

                draw_footer_dots(u8g2, SCREEN_IDX_DASHBOARD, SCREEN_IDX_TOTAL);
                return;
            }
            metric_count++;
        }
    }

    // Fallback if metric_index is out of range
    u8g2_SetFont(u8g2, u8g2_font_5x7_tf);
    u8g2_DrawStr(u8g2, 10, 36, "No data");
    draw_footer_dots(u8g2, SCREEN_IDX_DASHBOARD, SCREEN_IDX_TOTAL);
}

/**
 * @brief Get total number of metrics available for dashboard rotation
 */
int screen_get_metric_count(void)
{
    int count = 0;
    size_t sensor_count = sensor_manager_get_sensor_count();
    for (size_t s = 0; s < sensor_count; s++) {
        sensor_reading_t reading;
        if (sensor_manager_get_reading_by_index(s, &reading) != ESP_OK) continue;
        count++;  // Primary value
        if (!isnan(reading.value2)) {
            count++;  // Secondary value
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

    draw_footer_dots(u8g2, SCREEN_IDX_NETWORK, SCREEN_IDX_TOTAL);
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

    draw_footer_dots(u8g2, SCREEN_IDX_LORA, SCREEN_IDX_TOTAL);
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

    draw_footer_dots(u8g2, SCREEN_IDX_SYSTEM, SCREEN_IDX_TOTAL);
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

    draw_footer_dots(u8g2, SCREEN_IDX_NODES, SCREEN_IDX_TOTAL);
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

    draw_footer_dots(u8g2, SCREEN_IDX_SENSORS, SCREEN_IDX_TOTAL);
}

#endif /* CONFIG_STATUS_DISPLAY_ENABLED */

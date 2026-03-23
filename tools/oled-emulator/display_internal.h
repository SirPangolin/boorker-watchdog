/* Emulator version of display_internal.h
 * Re-declares the same screen functions and logo data
 * that the firmware's display_internal.h declares,
 * but without ESP-IDF dependencies.
 */
#pragma once

#include "u8g2.h"
#include "credentials.h"
#include <stdbool.h>
#include <stddef.h>

// Screen rendering functions (from display_screens.c)
void screen_splash(u8g2_t *u8g2, int throbber_phase);
void screen_first_boot(u8g2_t *u8g2, const credentials_t *creds, const char *ip_str);
void screen_dashboard_card(u8g2_t *u8g2, int metric_index);
int screen_get_metric_count(void);
void screen_alert(u8g2_t *u8g2, const char *source, const char *message, bool silenced);
void screen_network(u8g2_t *u8g2);
void screen_lora(u8g2_t *u8g2);
void screen_system(u8g2_t *u8g2);
void screen_nodes(u8g2_t *u8g2);
void screen_sensors(u8g2_t *u8g2);

// Logo data (from display_logo.c)
extern const uint8_t millie_logo_xbm[];
extern const int millie_logo_width;
extern const int millie_logo_height;

/**
 * @file main.c
 * @brief OLED screen emulator using u8g2 SDL backend
 *
 * Renders the actual firmware display_screens.c in a desktop window.
 * Press SPACE or RIGHT ARROW to cycle screens.
 * Press Q or ESC to quit.
 *
 * Usage: ./oled_emulator
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include "u8g2.h"

// Forward declarations for screen functions (from firmware display_screens.c)
extern void screen_splash(u8g2_t *u8g2, int throbber_phase);
extern void screen_first_boot(u8g2_t *u8g2, const void *creds, const char *ip_str);
extern void screen_dashboard_card(u8g2_t *u8g2, int metric_index);
extern int screen_get_metric_count(void);
extern void screen_alert(u8g2_t *u8g2, const char *source, const char *message, bool silenced);
extern void screen_network(u8g2_t *u8g2);
extern void screen_lora(u8g2_t *u8g2);
extern void screen_system(u8g2_t *u8g2);
extern void screen_nodes(u8g2_t *u8g2);
extern void screen_sensors(u8g2_t *u8g2);

// Mock credentials_get returns this
extern const void *credentials_get(void);

// u8g2 SDL key reader (from u8g2 sys/sdl)
extern int u8g_sdl_get_key(void);

static const char *screen_names[] = {
    "Splash",
    "First Boot",
    "Dashboard (temp)",
    "Dashboard (humidity)",
    "Dashboard (vibration)",
    "Alert (active)",
    "Alert (silenced)",
    "Network",
    "LoRa",
    "System",
    "Nodes",
    "Sensors",
};

#define SCREEN_COUNT 12

static void render_screen(u8g2_t *u8g2, int index) {
    u8g2_ClearBuffer(u8g2);

    switch (index) {
    case 0:
        screen_splash(u8g2, index % 3);
        break;
    case 1:
        screen_first_boot(u8g2, credentials_get(), "192.168.68.54");
        break;
    case 2:
        screen_dashboard_card(u8g2, 0);  // Temperature
        break;
    case 3:
        screen_dashboard_card(u8g2, 1);  // Humidity
        break;
    case 4:
        screen_dashboard_card(u8g2, 2);  // Vibration
        break;
    case 5:
        screen_alert(u8g2, "BASEMENT", "WATER DETECTED!", false);
        break;
    case 6:
        screen_alert(u8g2, "BASEMENT", "WATER DETECTED!", true);
        break;
    case 7:
        screen_network(u8g2);
        break;
    case 8:
        screen_lora(u8g2);
        break;
    case 9:
        screen_system(u8g2);
        break;
    case 10:
        screen_nodes(u8g2);
        break;
    case 11:
        screen_sensors(u8g2);
        break;
    }

    u8g2_SendBuffer(u8g2);
}

int main(void) {
    u8g2_t u8g2;

    // Setup u8g2 with SDL virtual display (128x64, 4x scale)
    u8g2_SetupBuffer_SDL_128x64_4(&u8g2, &u8g2_cb_r0);
    u8x8_InitDisplay(u8g2_GetU8x8(&u8g2));
    u8x8_SetPowerSave(u8g2_GetU8x8(&u8g2), 0);

    int current_screen = 0;

    printf("=== Boorker OLED Emulator ===\n");
    printf("SPACE/RIGHT: next screen | LEFT: previous | Q/ESC: quit\n");
    printf("Screen: %s\n", screen_names[current_screen]);

    render_screen(&u8g2, current_screen);

    while (1) {
        int key = u8g_sdl_get_key();

        if (key == 'q' || key == 27) {  // Q or ESC
            break;
        }

        if (key == ' ' || key == 275) {  // SPACE or RIGHT arrow
            current_screen = (current_screen + 1) % SCREEN_COUNT;
            printf("Screen: %s\n", screen_names[current_screen]);
            render_screen(&u8g2, current_screen);
        }

        if (key == 276) {  // LEFT arrow
            current_screen = (current_screen - 1 + SCREEN_COUNT) % SCREEN_COUNT;
            printf("Screen: %s\n", screen_names[current_screen]);
            render_screen(&u8g2, current_screen);
        }
    }

    printf("Emulator closed.\n");
    return 0;
}

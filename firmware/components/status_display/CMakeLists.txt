idf_component_register(
    SRCS "src/status_display.c"
         "src/display_screens.c"
         "src/display_hal.c"
         "src/display_logo.c"
         "src/display_console.c"
    INCLUDE_DIRS "include"
    REQUIRES button_driver event_bus sensor_manager status_buzzer
             credentials system_state nvs_flash log esp_timer
             driver main nixy4__u8g2 esp_wifi esp_netif
    PRIV_REQUIRES freertos
)

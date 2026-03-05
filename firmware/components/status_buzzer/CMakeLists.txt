idf_component_register(
    SRCS "src/status_buzzer.c"
         "src/buzzer_patterns.c"
         "src/buzzer_console.c"
    INCLUDE_DIRS "include"
    REQUIRES buzzer_driver event_bus nvs_flash esp_timer log console
)

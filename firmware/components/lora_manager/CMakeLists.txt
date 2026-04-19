idf_component_register(
    SRCS "src/lora_manager.c" "src/lora_console.c"
    INCLUDE_DIRS "include"
    REQUIRES sx1262_driver event_bus system_state console log
    PRIV_REQUIRES freertos esp_timer
)

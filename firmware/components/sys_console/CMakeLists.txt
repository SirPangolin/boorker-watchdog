idf_component_register(
    SRCS "src/sys_console.c"
    INCLUDE_DIRS "include"
    REQUIRES console system_state event_bus esp_timer
    PRIV_REQUIRES main freertos
)

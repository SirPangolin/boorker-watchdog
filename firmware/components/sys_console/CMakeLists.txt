# Build dependency list (tailscale_manager is optional)
set(SYS_CONSOLE_REQUIRES console esp_timer credentials wifi_manager event_bus)

if(CONFIG_TS_MGR_ENABLED)
    list(APPEND SYS_CONSOLE_REQUIRES tailscale_manager)
endif()

idf_component_register(
    SRCS "src/sys_console.c"
    INCLUDE_DIRS "include"
    REQUIRES ${SYS_CONSOLE_REQUIRES}
    PRIV_REQUIRES main freertos
)

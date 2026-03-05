# Build dependency list (tailscale_manager is optional)
set(SYSTEM_CONSOLE_REQUIRES console esp_timer credentials wifi_manager)

if(CONFIG_TS_MGR_ENABLED)
    list(APPEND SYSTEM_CONSOLE_REQUIRES tailscale_manager)
endif()

idf_component_register(
    SRCS "src/system_console.c"
    INCLUDE_DIRS "include"
    REQUIRES ${SYSTEM_CONSOLE_REQUIRES}
    PRIV_REQUIRES main freertos
)

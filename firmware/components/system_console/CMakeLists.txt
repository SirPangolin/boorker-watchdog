idf_component_register(
    SRCS "src/system_console.c"
    INCLUDE_DIRS "include"
    REQUIRES console esp_timer device_identity wifi_manager tailscale_manager
    PRIV_REQUIRES main
)

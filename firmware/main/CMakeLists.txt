idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES wifi_manager tailscale_manager console nvs_flash
    PRIV_REQUIRES credentials system_state event_bus web_auth web_server system_console status_led
)

idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    REQUIRES wifi_manager tailscale_manager console nvs_flash
    PRIV_REQUIRES credentials system_state event_bus web_auth http_server sys_console status_led status_buzzer status_display sensor_manager sw420_driver ota_manager
)

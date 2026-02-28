idf_component_register(
    SRCS
        "src/tailscale_manager.c"
        "src/tailscale_nvs.c"
        "src/tailscale_console.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash console esp_event esp_timer log microlink
    PRIV_REQUIRES freertos
)

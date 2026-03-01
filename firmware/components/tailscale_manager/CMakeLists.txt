if(CONFIG_TS_MGR_ENABLED)
    idf_component_register(
        SRCS
            "src/tailscale_manager.c"
            "src/tailscale_nvs.c"
            "src/tailscale_console.c"
        INCLUDE_DIRS "include"
        REQUIRES nvs_flash console esp_event esp_timer log microlink
        PRIV_REQUIRES freertos
    )
else()
    # Empty component when Tailscale is disabled
    idf_component_register()
endif()

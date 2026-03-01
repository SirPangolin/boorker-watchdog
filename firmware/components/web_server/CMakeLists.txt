idf_component_register(
    SRCS "src/web_server.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_http_server web_auth device_identity system_console joltwallet__littlefs json esp_wifi log esp_timer
    PRIV_REQUIRES main
)

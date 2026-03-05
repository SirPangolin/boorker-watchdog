idf_component_register(
    SRCS "src/web_auth.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_http_server device_identity mbedtls log device_state event_bus
)

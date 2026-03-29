idf_component_register(
    SRCS "src/web_auth.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_http_server secrets mbedtls log system_state event_bus
)

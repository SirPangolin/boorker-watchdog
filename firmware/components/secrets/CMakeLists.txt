idf_component_register(
    SRCS "src/secrets.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_hw_support esp_system log mbedtls
)

idf_component_register(
    SRCS "src/sw420_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES driver log nvs_flash freertos
)

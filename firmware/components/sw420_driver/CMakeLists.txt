idf_component_register(
    SRCS
        "src/sw420_driver.c"
        "src/sw420_console.c"
    INCLUDE_DIRS "include"
    REQUIRES driver console log nvs_flash freertos
)

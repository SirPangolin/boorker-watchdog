idf_component_register(
    SRCS "src/button_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES driver log
    PRIV_REQUIRES esp_timer
)

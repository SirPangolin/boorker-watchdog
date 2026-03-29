idf_component_register(
    SRCS "src/sx1262_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES driver log
    PRIV_REQUIRES freertos
)

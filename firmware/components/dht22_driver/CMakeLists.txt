idf_component_register(
    SRCS "src/dht22_driver.c"
    INCLUDE_DIRS "include"
    REQUIRES driver log freertos
)

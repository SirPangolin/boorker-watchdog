idf_component_register(
    SRCS "src/andon_service.c"
    INCLUDE_DIRS "include"
    REQUIRES device_state log
)

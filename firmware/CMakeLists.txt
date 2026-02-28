cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS
    components
    components/microlink/components
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(boorker-watchdog)

# Create LittleFS image from www directory
littlefs_create_partition_image(storage ../www FLASH_IN_PROJECT)
cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS
        "main.c"
        "sensor_pir.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        hivekit
        nvs_flash
        driver
        freertos
)

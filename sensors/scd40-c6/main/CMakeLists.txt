cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS
        "main.c"
        "sensor_scd40.c"
    INCLUDE_DIRS
        "."
    REQUIRES
        hivekit
        nvs_flash
        driver
        freertos
        esp_driver_i2c
        esp_system
)

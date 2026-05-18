cmake_minimum_required(VERSION 3.16)

idf_component_register(
    SRCS
        "hivekit_core.c"
        "hivekit_button.c"
        "hivekit_led.c"
        "hivekit_reporting.c"
    INCLUDE_DIRS
        "include"
    REQUIRES
        nvs_flash
        espressif__esp_zigbee_lib
        freertos
        # espressif/button and espressif/led_indicator are declared in
        # the sensor firmware's idf_component.yml and are available via
        # IDF Component Manager. They show up as managed_components/ at
        # build time. If you see "cannot find component" errors, run:
        #   idf.py update-dependencies
)

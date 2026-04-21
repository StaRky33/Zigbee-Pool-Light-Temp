idf_component_register(
    SRCS
        "esp_zb_pool.c"
        "ntc.c"
        "relay.c"
    INCLUDE_DIRS
        "."
        "${CMAKE_CURRENT_SOURCE_DIR}/../managed_components/espressif__esp-zigbee-lib/include"
    REQUIRES
        driver
        esp_adc
        esp_timer
        nvs_flash
        log
        esp_system
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE ZB_ED_ROLE)
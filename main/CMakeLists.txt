idf_component_register(
    SRCS
        "src/app_main.c"
        "src/config.c"
        "src/ble_client.c"
        "src/river2_auth.c"
        "src/river2_crc.c"
        "src/river2_ecdh.c"
        "src/river2_packet.c"
    INCLUDE_DIRS "include"
    REQUIRES
        nvs_flash
        bt
        mbedtls
        micro-ecc
        esp_hw_support
        esp_timer)

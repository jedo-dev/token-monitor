idf_component_register(
    SRCS main.c ui.c net.c battery.c
         fonts/tm_m12.c fonts/tm_m16.c fonts/tm_sb20.c fonts/tm_b36.c fonts/tm_b48.c
    EMBED_FILES icons/weather.bin icons/sunrise.bin icons/sunset.bin
                icons/close_lg.bin icons/check.bin icons/logo.bin
    INCLUDE_DIRS .
    REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_http_client json)

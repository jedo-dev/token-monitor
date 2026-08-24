idf_component_register(
    SRCS main.c ui.c net.c battery.c
         fonts/font_ru_12.c fonts/font_ru_16.c fonts/font_ru_20.c
    INCLUDE_DIRS .
    REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_http_client json)

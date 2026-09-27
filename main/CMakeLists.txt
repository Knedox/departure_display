idf_component_register(
    SRCS "departures_parser.cpp" "power.cpp" "display.cpp" "departures_api.cpp" "wifi_manager.cpp" "main.cpp"
    REQUIRES
        esp_timer
        u8g2_st7305
        esp_wifi
        esp_event
        nvs_flash
        esp_http_client
        esp_adc
    INCLUDE_DIRS "./")

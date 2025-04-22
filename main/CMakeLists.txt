# main/CMakeLists.txt
idf_component_register(SRCS "main.c" "led_handler.c"
                    INCLUDE_DIRS "." # Include common_defs.h, led_handler.h
                    REQUIRES nvs_flash esp_netif esp_event esp_wifi # For main init and MAC
                             json # For JSON parsing in main's callback
                             # Component dependencies:
                             uart_comm
                             wifi_conn
                             mqtt_comm
                             # Other dependencies:
                             freertos log esp_system driver) # Base dependencies
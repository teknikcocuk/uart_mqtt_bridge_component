# components/mqtt_comm/CMakeLists.txt
idf_component_register(SRCS "mqtt_comm.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos log mqtt # Use the ESP-IDF MQTT component
                             # esp_wifi is needed only for default client_id generation
                    PRIV_REQUIRES esp_wifi ) # wifi_conn not strictly needed if it guarantees netif/event loop
# components/wifi_conn/CMakeLists.txt
idf_component_register(SRCS "wifi_conn.c"
                    INCLUDE_DIRS "include"
                    REQUIRES freertos esp_wifi esp_event log esp_netif lwip)
                    # NVS is required by WiFi stack, but should be initialized by main app
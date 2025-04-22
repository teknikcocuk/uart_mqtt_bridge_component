// main/main.c
#include <stdio.h>
#include <inttypes.h> // For PRIu32 macro
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
// #include "freertos/event_groups.h" // No longer directly needed by main
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_system.h"
#include "esp_wifi.h" // Needed for MAC address
#include "cJSON.h"    // For parsing UART data

// Include component headers
#include "uart_comm.h"
#include "wifi_conn.h"
#include "mqtt_comm.h"

// Include local headers
#include "common_defs.h"
#include "led_handler.h"

static const char *TAG = "MAIN_APP";

// Define LED queue handle (declared as extern in common_defs.h)
QueueHandle_t led_command_queue = NULL;

// Buffer for device-specific MQTT subscription topic
static char mqtt_sub_topic_str[64];
static char mac_address_str[18] = {0};

// --- Callback Implementations ---

// Callback for UART data reception
void app_uart_rx_callback(const uint8_t *data, size_t len) {
    ESP_LOGI(TAG, "UART RX Callback: Received %d bytes", len);
    led_command_t led_cmd = LED_CMD_UART_RX_RECEIVED;
    xQueueSend(led_command_queue, &led_cmd, pdMS_TO_TICKS(10));

    // Need mutable buffer for cJSON if it modifies input (it shouldn't for parse)
    // Add null terminator for string parsing
    char *json_string = malloc(len + 1);
    if (!json_string) {
        ESP_LOGE(TAG, "Failed to allocate buffer for JSON string");
        return;
    }
    memcpy(json_string, data, len);
    json_string[len] = '\0';

    // --- Parse JSON ---
    cJSON *root = cJSON_Parse((const char *)json_string);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON: %s", cJSON_GetErrorPtr());
        const char *err_msg = "Error: Invalid JSON\r\n";
        uart_comm_transmit((const uint8_t *)err_msg, strlen(err_msg));
        free(json_string);
        return;
    }

    cJSON *topic_item = cJSON_GetObjectItem(root, "topic");
    cJSON *payload_item = cJSON_GetObjectItem(root, "payload");

    if (!cJSON_IsString(topic_item) || !topic_item->valuestring ||
        !cJSON_IsString(payload_item) || !payload_item->valuestring)
    {
        ESP_LOGE(TAG, "JSON format error: 'topic' or 'payload' missing/invalid.");
        const char *err_msg = "Error: Missing/Invalid 'topic' or 'payload'\r\n";
        uart_comm_transmit((const uint8_t *)err_msg, strlen(err_msg));
    } else {
        // Construct the full topic including the base
        char full_topic[128]; // Adjust size as needed
        snprintf(full_topic, sizeof(full_topic), "%s%s", APP_MQTT_PUB_BASE_TOPIC, topic_item->valuestring);

        ESP_LOGI(TAG, "Parsed UART JSON - Topic: '%s', Payload: '%s'", full_topic, payload_item->valuestring);

        // Publish using MQTT component's function
        esp_err_t pub_ret = mqtt_comm_publish(full_topic, payload_item->valuestring, -1, 1, 0); // len=-1 for strlen
        if (pub_ret == ESP_OK) {
            ESP_LOGI(TAG, "Message queued for MQTT publish.");
            const char *ok_msg = "OK: Sent to MQTT Queue\r\n";
             uart_comm_transmit((const uint8_t *)ok_msg, strlen(ok_msg));
        } else {
             ESP_LOGE(TAG, "Failed to queue message for MQTT publish (Error: %s)", esp_err_to_name(pub_ret));
            const char *fail_msg = "Error: Failed to send to MQTT\r\n";
             uart_comm_transmit((const uint8_t *)fail_msg, strlen(fail_msg));
        }
    }

    cJSON_Delete(root);
    free(json_string);
}

// Callback for WiFi status changes
void app_wifi_status_callback(wifi_conn_status_t status, const esp_netif_ip_info_t *ip_info) {
    led_command_t led_cmd;
    switch (status) {
        case WIFI_CONN_STATUS_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi Disconnected.");
            led_cmd = LED_CMD_WIFI_CONNECTING; // Indicate attempting to reconnect
            xQueueSend(led_command_queue, &led_cmd, pdMS_TO_TICKS(10));
            break;
        case WIFI_CONN_STATUS_CONNECTING:
            ESP_LOGI(TAG, "WiFi Connecting...");
            led_cmd = LED_CMD_WIFI_CONNECTING;
            xQueueSend(led_command_queue, &led_cmd, pdMS_TO_TICKS(10));
            break;
        case WIFI_CONN_STATUS_CONNECTED_GOT_IP:
            ESP_LOGI(TAG, "WiFi Connected. IP: " IPSTR, IP2STR(&ip_info->ip));
            led_cmd = LED_CMD_WIFI_CONNECTED; // Indicate WiFi OK, MQTT state pending
            xQueueSend(led_command_queue, &led_cmd, pdMS_TO_TICKS(10));
            // Note: MQTT client will start connecting automatically now
            break;
        case WIFI_CONN_STATUS_CONNECTION_FAILED:
             ESP_LOGE(TAG, "WiFi Connection Failed Permanently (or max retries).");
             led_cmd = LED_CMD_ERROR; // Or a specific WiFi error LED pattern
             xQueueSend(led_command_queue, &led_cmd, pdMS_TO_TICKS(10));
             break;

    }
}

// Callback for MQTT status changes
void app_mqtt_status_callback(mqtt_conn_status_t status) {
    led_command_t led_cmd;
    switch (status) {
        case MQTT_CONN_STATUS_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT Disconnected.");
            // Revert LED to WiFi connected state (as WiFi is likely still up)
            if (wifi_conn_is_connected()) {
                led_cmd = LED_CMD_WIFI_CONNECTED;
            } else {
                led_cmd = LED_CMD_WIFI_CONNECTING; // Or appropriate WiFi state
            }
             xQueueSend(led_command_queue, &led_cmd, pdMS_TO_TICKS(10));
            break;
        case MQTT_CONN_STATUS_CONNECTING:
            // ESP-IDF client handles this, but we could set LED state if needed
             ESP_LOGI(TAG, "MQTT Connecting...");
            break;
        case MQTT_CONN_STATUS_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected.");
            led_cmd = LED_CMD_MQTT_CONNECTED;
            xQueueSend(led_command_queue, &led_cmd, pdMS_TO_TICKS(10));
            // Subscribe to the device-specific topic
             if (strlen(mqtt_sub_topic_str) > 0) {
                 ESP_LOGI(TAG, "Subscribing to: %s", mqtt_sub_topic_str);
                 esp_err_t sub_ret = mqtt_comm_subscribe(mqtt_sub_topic_str, 1);
                 if (sub_ret != ESP_OK) {
                     ESP_LOGE(TAG, "Failed to queue subscribe request for %s (Error: %s)", mqtt_sub_topic_str, esp_err_to_name(sub_ret));
                 }
             } else {
                  ESP_LOGE(TAG, "Subscription topic not generated!");
             }

            break;
        case MQTT_CONN_STATUS_ERROR:
            ESP_LOGE(TAG, "MQTT Connection Error.");
            // Revert LED state
             if (wifi_conn_is_connected()) {
                led_cmd = LED_CMD_WIFI_CONNECTED;
            } else {
                led_cmd = LED_CMD_WIFI_CONNECTING;
            }
             xQueueSend(led_command_queue, &led_cmd, pdMS_TO_TICKS(10));
            break;
    }
}

// Callback for MQTT received data
void app_mqtt_data_callback(const char *topic, size_t topic_len, const char *data, size_t data_len) {
    ESP_LOGI(TAG, "MQTT RX Callback: Topic='%.*s', Data='%.*s'", topic_len, topic, data_len, data);

    led_command_t led_cmd = LED_CMD_MQTT_RX_RECEIVED;
    xQueueSend(led_command_queue, &led_cmd, pdMS_TO_TICKS(10));

    // Check if the topic matches our subscription
    if (topic_len == strlen(mqtt_sub_topic_str) &&
        strncmp(topic, mqtt_sub_topic_str, topic_len) == 0)
    {
        ESP_LOGI(TAG, "Received data on subscribed topic.");
        // Format and send to UART
        char tx_buffer[data_len + 32]; // Adjust buffer size as needed
        int len = snprintf(tx_buffer, sizeof(tx_buffer), "MQTT Data: %.*s\r\n", data_len, data);
        if (len > 0) {
            esp_err_t uart_ret = uart_comm_transmit((const uint8_t *)tx_buffer, len);
            if (uart_ret == ESP_OK) {
                 ESP_LOGI(TAG, "Sent MQTT data to UART.");
            } else {
                 ESP_LOGE(TAG, "Failed to send MQTT data to UART.");
            }
        } else {
             ESP_LOGE(TAG, "Failed to format MQTT data for UART TX");
        }
    } else {
        ESP_LOGW(TAG, "Received data on unexpected topic: %.*s", topic_len, topic);
    }
}


// Get MAC address string helper
static void get_mac_address_str()
{
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac); // Assumes STA interface for MAC
    snprintf(mac_address_str, sizeof(mac_address_str), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_LOGI(TAG, "Device MAC Address: %s", mac_address_str);
}


void app_main(void)
{
    ESP_LOGI(TAG, "[APP] Startup..");
    ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "[APP] IDF version: %s", esp_get_idf_version());

    // Set log levels (optional)
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set("MQTT_CLIENT", ESP_LOG_VERBOSE); // Log ESP-IDF MQTT client
    esp_log_level_set("MQTT_COMM", ESP_LOG_VERBOSE);   // Log our MQTT component
    esp_log_level_set("WIFI_CONN", ESP_LOG_VERBOSE);   // Log our WiFi component
    esp_log_level_set("UART_COMM", ESP_LOG_VERBOSE);   // Log our UART component
    esp_log_level_set("LED_HANDLER", ESP_LOG_VERBOSE); // Log LED handler
    esp_log_level_set("MAIN_APP", ESP_LOG_INFO);

    // --- Initialize NVS ---
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // --- Initialize TCP/IP stack and default event loop ---
    // These are prerequisites for WiFi and MQTT components
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // --- Create LED Queue ---
    ESP_LOGI(TAG, "Creating LED Command Queue...");
    led_command_queue = xQueueCreate(15, sizeof(led_command_t));
     if (!led_command_queue) {
        ESP_LOGE(TAG, "Failed to create LED queue!");
        // Halt or handle error appropriately
        return;
    }

    // --- Initialize LED Handler ---
    ESP_LOGI(TAG, "Initializing LED Handler...");
    ret = led_init_and_start_task(led_command_queue);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize LED handler! Continuing without LED indication.");
        // Continue execution if LED is non-critical
    }

    // --- Initialize WiFi Component ---
    ESP_LOGI(TAG, "Initializing WiFi Component...");
    // WiFi init needs to happen before MQTT init and before getting MAC address
    ret = wifi_conn_init_sta(APP_WIFI_SSID, APP_WIFI_PASS, app_wifi_status_callback);
     if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize WiFi component! Halting.");
        // Cannot proceed without WiFi for MQTT
        return;
    }

    // --- Prepare MQTT Subscription Topic ---
    get_mac_address_str(); // Get MAC after WiFi stack is initialized
    snprintf(mqtt_sub_topic_str, sizeof(mqtt_sub_topic_str), "%s%s", APP_MQTT_SUB_BASE_TOPIC, mac_address_str);


    // --- Initialize MQTT Component ---
    ESP_LOGI(TAG, "Initializing MQTT Component...");
    mqtt_comm_config_t mqtt_config = {
        .broker_uri = APP_MQTT_BROKER_URI,
        // .client_id = APP_MQTT_CLIENT_ID,   // NULL uses default
        // .username = APP_MQTT_USERNAME,     // NULL for none
        // .password = APP_MQTT_PASSWORD      // NULL for none
    };
    ret = mqtt_comm_init(&mqtt_config, app_mqtt_status_callback, app_mqtt_data_callback);
     if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize MQTT component! Features requiring MQTT might fail.");
        // Decide if the application can continue without MQTT
    }

    // --- Initialize UART Component ---
    ESP_LOGI(TAG, "Initializing UART Component...");
    uart_comm_config_t uart_config = {
        .port = APP_UART_NUM,
        .tx_pin = APP_UART_TX_PIN,
        .rx_pin = APP_UART_RX_PIN,
        .baud_rate = APP_UART_BAUD_RATE,
        .rx_buffer_size = APP_UART_RX_BUF_SIZE,
        .tx_buffer_size = APP_UART_TX_BUF_SIZE,
        .queue_size = APP_UART_QUEUE_SIZE
    };
    ret = uart_comm_init(&uart_config, app_uart_rx_callback);
     if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UART component!");
        // Decide if the application can continue without UART
    }

    ESP_LOGI(TAG, "Main task finished initialization. Components running.");

    // Main task can now idle or perform other duties
     while(1) {
         vTaskDelay(pdMS_TO_TICKS(30000)); // Check every 30 seconds
         ESP_LOGI(TAG, "[APP] Free memory: %" PRIu32 " bytes", esp_get_free_heap_size());
         ESP_LOGI(TAG, "[APP] MQTT Connected: %s", mqtt_comm_is_connected() ? "Yes" : "No");
         ESP_LOGI(TAG, "[APP] WiFi Connected: %s", wifi_conn_is_connected() ? "Yes" : "No");
     }
}
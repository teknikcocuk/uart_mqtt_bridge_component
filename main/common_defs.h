// main/common_defs.h
#ifndef COMMON_DEFS_H
#define COMMON_DEFS_H

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"

// Queue handle for LED commands (still needed by main app logic)
extern QueueHandle_t led_command_queue;

// LED Commands Enum (Unchanged)
typedef enum {
    LED_CMD_OFF,
    LED_CMD_WIFI_CONNECTING,
    LED_CMD_WIFI_CONNECTED, // WiFi Connected, MQTT might be connecting/disconnected
    LED_CMD_MQTT_CONNECTED, // WiFi and MQTT are connected
    LED_CMD_UART_RX_RECEIVED,
    LED_CMD_MQTT_RX_RECEIVED,
    LED_CMD_ERROR // Optional: Generic error state
} led_command_t;

// --- Configuration (Hardcoded - Replace with your details!) ---
// WiFi
#define APP_WIFI_SSID "Nearloc.Private.Main" // <<< CHANGE THIS
#define APP_WIFI_PASS "1928374650"           // <<< CHANGE THIS

// MQTT
#define APP_MQTT_BROKER_URI "mqtt://mqtt.eclipseprojects.io" // <<< CHANGE OR CONFIRM
#define APP_MQTT_PUB_BASE_TOPIC "pub/data/"                  // Base for publishing from UART
#define APP_MQTT_SUB_BASE_TOPIC "sub/data/"                  // Base for subscribing
// #define APP_MQTT_CLIENT_ID NULL // Let component generate default
// #define APP_MQTT_USERNAME NULL
// #define APP_MQTT_PASSWORD NULL

// UART (Configuration moved to main.c for uart_comm_init)
#define APP_UART_NUM UART_NUM_2
#define APP_UART_TX_PIN (17)
#define APP_UART_RX_PIN (16)
#define APP_UART_BAUD_RATE 115200
#define APP_UART_RX_BUF_SIZE (1024) // Ring buffer size for driver
#define APP_UART_TX_BUF_SIZE (0)    // No TX ring buffer
#define APP_UART_QUEUE_SIZE (0)     // Default event queue

// LED
#define APP_LED_GPIO (GPIO_NUM_2) // Common built-in LED GPIO
#define APP_LED_TASK_STACK (2048) // Stack for LED handler task in main
// --- End Configuration ---

#endif // COMMON_DEFS_H
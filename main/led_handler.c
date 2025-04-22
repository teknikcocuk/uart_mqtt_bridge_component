// main/led_handler.c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_log.h"

// Include local headers
#include "led_handler.h" // Include own header
#include "common_defs.h" // For APP_LED_GPIO, APP_LED_TASK_STACK, led_command_t

// TAG updated for consistency (optional)
static const char *TAG = "LED_HANDLER";

// Task function to control the LED based on commands received via queue
static void led_control_task(void *pvParameters)
{
    QueueHandle_t cmd_queue = (QueueHandle_t)pvParameters;
    led_command_t received_cmd;
    // Track logical *steady* state for restoring after blinks
    uint8_t current_led_steady_state = LED_CMD_OFF;

    ESP_LOGI(TAG, "LED control task started (GPIO %d).", APP_LED_GPIO);

    // Ensure LED is off initially
    gpio_set_level(APP_LED_GPIO, 0);

    while (1)
    {
        // Wait indefinitely for a command
        if (xQueueReceive(cmd_queue, &received_cmd, portMAX_DELAY))
        {
            ESP_LOGD(TAG, "Received LED command: %d", received_cmd);

            // --- Execute LED Action/Animation ---

            // Store previous steady state before potentially transient animations
            bool was_steady_on = (current_led_steady_state == LED_CMD_MQTT_CONNECTED);

            // Turn off LED before starting a new pattern (simplifies logic)
            // Exception: If going from MQTT_CONNECTED to a blink, we handle restore later.
            if (received_cmd != LED_CMD_UART_RX_RECEIVED && received_cmd != LED_CMD_MQTT_RX_RECEIVED) {
                 gpio_set_level(APP_LED_GPIO, 0);
            }


            switch (received_cmd)
            {
                case LED_CMD_WIFI_CONNECTING:
                    current_led_steady_state = received_cmd; // This is a persistent state until changed
                    ESP_LOGD(TAG, "LED: WiFi Connecting - Slow Blink");
                    // Simple blocking slow blink for indication
                    // Note: This will block processing other commands for 1s
                    gpio_set_level(APP_LED_GPIO, 1);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    gpio_set_level(APP_LED_GPIO, 0);
                    vTaskDelay(pdMS_TO_TICKS(500));
                    // For continuous blinking, a non-blocking timer/state machine would be better
                    break;

                case LED_CMD_WIFI_CONNECTED:
                    current_led_steady_state = received_cmd; // Persistent state
                    ESP_LOGD(TAG, "LED: WiFi Connected - Solid ON briefly then OFF");
                    // Solid ON briefly then off (indicates WiFi OK, MQTT pending)
                    gpio_set_level(APP_LED_GPIO, 1);
                    vTaskDelay(pdMS_TO_TICKS(1000)); // Show solid for 1 sec
                    gpio_set_level(APP_LED_GPIO, 0);
                     // The LED stays off after this indication, until MQTT connects or disconnects
                    break;

                case LED_CMD_MQTT_CONNECTED:
                    current_led_steady_state = received_cmd; // Persistent state
                    ESP_LOGD(TAG, "LED: MQTT Connected - Solid ON");
                    gpio_set_level(APP_LED_GPIO, 1); // Solid ON for fully operational
                    // Keep it ON - next state change (disconnect, error) will turn it off/change it
                    break;

                case LED_CMD_UART_RX_RECEIVED:
                    // Fast double blink, then return to previous *steady* state
                    ESP_LOGD(TAG, "LED: UART RX - Fast Double Blink");
                    // Turn off if it was steady on, otherwise it's already off
                    if (was_steady_on) gpio_set_level(APP_LED_GPIO, 0);
                    vTaskDelay(pdMS_TO_TICKS(50)); // Small delay before blinking

                    for (int i = 0; i < 2; i++) {
                        gpio_set_level(APP_LED_GPIO, 1);
                        vTaskDelay(pdMS_TO_TICKS(75)); // Slightly longer ON for visibility
                        gpio_set_level(APP_LED_GPIO, 0);
                        vTaskDelay(pdMS_TO_TICKS(75));
                    }
                    // Restore previous steady state
                    if (was_steady_on) {
                        vTaskDelay(pdMS_TO_TICKS(50)); // Small delay after blinking
                        gpio_set_level(APP_LED_GPIO, 1); // Restore solid ON
                    }
                    break;

                case LED_CMD_MQTT_RX_RECEIVED:
                    // Quick single pulse, then return to previous *steady* state
                    ESP_LOGD(TAG, "LED: MQTT RX - Quick Pulse");
                    if (was_steady_on) gpio_set_level(APP_LED_GPIO, 0);
                    vTaskDelay(pdMS_TO_TICKS(50));

                    gpio_set_level(APP_LED_GPIO, 1);
                    vTaskDelay(pdMS_TO_TICKS(150)); // Pulse duration
                    gpio_set_level(APP_LED_GPIO, 0);

                    // Restore previous steady state
                    if (was_steady_on) {
                         vTaskDelay(pdMS_TO_TICKS(50));
                        gpio_set_level(APP_LED_GPIO, 1); // Restore solid ON
                    }
                    break;

                case LED_CMD_ERROR: // Handle generic error state
                    current_led_steady_state = received_cmd; // Persistent error state
                    ESP_LOGD(TAG, "LED: Error State - Very Fast Blink");
                     // Example: Fast continuous blink indicates error
                     // Note: Like WIFI_CONNECTING, this is blocking. Consider timer for continuous.
                    gpio_set_level(APP_LED_GPIO, 1);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    gpio_set_level(APP_LED_GPIO, 0);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    break;

                case LED_CMD_OFF:
                default:
                    ESP_LOGD(TAG, "LED: Turning OFF");
                    current_led_steady_state = LED_CMD_OFF; // Persistent state
                    gpio_set_level(APP_LED_GPIO, 0); // Turn off explicitly
                    break;
            }
        }
         // Add a small delay to prevent this task from hogging CPU if queue is spammed
         // Only needed if the switch cases don't inherently have delays.
         // vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// Initialize LED GPIO and start the control task
esp_err_t led_init_and_start_task(QueueHandle_t cmd_queue)
{
    if (cmd_queue == NULL) {
        ESP_LOGE(TAG, "Command queue handle is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing LED GPIO %d", APP_LED_GPIO);

    // Configure GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << APP_LED_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
         ESP_LOGE(TAG, "Failed to configure LED GPIO (%s)", esp_err_to_name(ret));
        return ret;
    }

    // Set initial level
    gpio_set_level(APP_LED_GPIO, 0);

    // Create the LED control task
    BaseType_t task_created = xTaskCreate(led_control_task,
                                          "led_control_task",   // Task name
                                          APP_LED_TASK_STACK,   // Stack size from common_defs.h
                                          (void *)cmd_queue,    // Pass queue handle as parameter
                                          5,                    // Task priority
                                          NULL);                // Task handle (optional)

    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create LED control task");
        // No GPIO cleanup needed here as gpio_config succeeded
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "LED handler initialized and task started.");
    return ESP_OK;
}
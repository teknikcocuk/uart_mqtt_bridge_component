// main/led_handler.h
#ifndef LED_HANDLER_H
#define LED_HANDLER_H

#include "esp_err.h"
#include "common_defs.h" // For led_command_t and QueueHandle_t extern declaration

/**
 * @brief Initialize the LED GPIO and start the LED control task.
 *
 * @param cmd_queue Handle to the queue used to send commands to the LED task.
 *                  This queue should be created in the main application.
 * @return esp_err_t ESP_OK on success, or an error code.
 */
esp_err_t led_init_and_start_task(QueueHandle_t cmd_queue);

#endif // LED_HANDLER_H
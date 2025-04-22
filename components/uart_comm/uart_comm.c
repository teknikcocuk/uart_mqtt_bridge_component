// components/uart_comm/uart_comm.c
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "uart_comm.h" // Include own header

static const char *TAG = "UART_COMM";

// Configuration and state
static uart_comm_config_t s_uart_config;
static bool s_uart_initialized = false;
static TaskHandle_t s_uart_rx_task_handle = NULL;
static uart_comm_rx_callback_t s_rx_callback = NULL;
static SemaphoreHandle_t s_tx_mutex = NULL;

// Forward declaration
static void uart_rx_task(void *pvParameters);

/**
 * @brief Default weak implementation of the RX callback.
 *        The application should provide its own implementation.
 */
__attribute__((weak)) void uart_comm_on_receive(const uint8_t *data, size_t len) {
    ESP_LOGW(TAG, "uart_comm_on_receive (weak): Received %d bytes, but no handler implemented in main app.", len);
    // Default implementation does nothing or logs a warning.
}


esp_err_t uart_comm_init(const uart_comm_config_t *config, uart_comm_rx_callback_t rx_callback) {
    if (s_uart_initialized) {
        ESP_LOGW(TAG, "UART already initialized.");
        return ESP_OK;
    }
    if (!config || !rx_callback) {
        return ESP_ERR_INVALID_ARG;
    }

    s_uart_config = *config; // Copy config
    s_rx_callback = rx_callback;

    uart_config_t uart_drv_config = {
        .baud_rate = s_uart_config.baud_rate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    ESP_LOGI(TAG, "Initializing UART%d (TX:%d, RX:%d, Baud:%d)",
             s_uart_config.port, s_uart_config.tx_pin, s_uart_config.rx_pin, s_uart_config.baud_rate);

    esp_err_t ret = uart_param_config(s_uart_config.port, &uart_drv_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_param_config failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_set_pin(s_uart_config.port, s_uart_config.tx_pin, s_uart_config.rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_set_pin failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = uart_driver_install(s_uart_config.port, s_uart_config.rx_buffer_size, s_uart_config.tx_buffer_size,
                              s_uart_config.queue_size, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_install failed: %s", esp_err_to_name(ret));
        return ret;
    }

    s_tx_mutex = xSemaphoreCreateMutex();
    if (s_tx_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create TX mutex");
        uart_driver_delete(s_uart_config.port);
        return ESP_FAIL;
    }

    // Create the UART RX task
    BaseType_t task_created = xTaskCreate(uart_rx_task, "uart_rx_task", 4096, NULL, 10, &s_uart_rx_task_handle);
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create UART RX task");
        vSemaphoreDelete(s_tx_mutex);
        s_tx_mutex = NULL;
        uart_driver_delete(s_uart_config.port);
        return ESP_FAIL;
    }

    s_uart_initialized = true;
    ESP_LOGI(TAG, "UART%d initialized successfully.", s_uart_config.port);
    return ESP_OK;
}

esp_err_t uart_comm_transmit(const uint8_t *data, size_t len) {
    if (!s_uart_initialized) {
        ESP_LOGE(TAG, "UART not initialized, cannot transmit.");
        return ESP_FAIL;
    }
    if (!data || len == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = ESP_FAIL;
    if (xSemaphoreTake(s_tx_mutex, portMAX_DELAY) == pdTRUE) {
        int written = uart_write_bytes(s_uart_config.port, data, len);
        if (written == (int)len) {
            ret = ESP_OK;
        } else {
            ESP_LOGE(TAG, "UART write failed (wrote %d, expected %d)", written, len);
            ret = ESP_FAIL;
        }
        xSemaphoreGive(s_tx_mutex);
    } else {
        ESP_LOGE(TAG, "Could not obtain TX mutex");
        ret = ESP_FAIL; // Or a specific error like ESP_ERR_TIMEOUT
    }
    return ret;
}

esp_err_t uart_comm_deinit(void) {
    if (!s_uart_initialized) {
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Deinitializing UART%d", s_uart_config.port);

    if (s_uart_rx_task_handle) {
        vTaskDelete(s_uart_rx_task_handle);
        s_uart_rx_task_handle = NULL;
    }

    esp_err_t ret = uart_driver_delete(s_uart_config.port);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "uart_driver_delete failed: %s", esp_err_to_name(ret));
        // Continue cleanup even if delete fails
    }

    if (s_tx_mutex) {
        vSemaphoreDelete(s_tx_mutex);
        s_tx_mutex = NULL;
    }

    s_uart_initialized = false;
    s_rx_callback = NULL;
    memset(&s_uart_config, 0, sizeof(s_uart_config));

    ESP_LOGI(TAG, "UART%d deinitialized.", s_uart_config.port); // Use stored port before clearing
    return ret; // Return result of driver delete
}

// --- Internal Task ---

static void uart_rx_task(void *pvParameters) {
    uint8_t *rx_buffer = (uint8_t *)malloc(s_uart_config.rx_buffer_size);
    if (rx_buffer == NULL) {
        ESP_LOGE(TAG, "Failed to allocate RX buffer for task");
        // Attempt to deinitialize cleanly? Or just notify error?
        s_uart_initialized = false; // Mark as non-functional
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "UART RX task started for UART%d.", s_uart_config.port);

    while (1) {
        // Read data from the UART
        int len = uart_read_bytes(s_uart_config.port, rx_buffer, s_uart_config.rx_buffer_size - 1, pdMS_TO_TICKS(100)); // Wait max 100ms

        if (len > 0) {
            // Null-terminate (optional, depends on expected data type)
            // rx_buffer[len] = '\0';
            ESP_LOGD(TAG, "UART%d Received %d bytes", s_uart_config.port, len);
            if (s_rx_callback) {
                // Call the application-provided callback
                s_rx_callback(rx_buffer, len);
            } else {
                 // Should not happen if init succeeded, but check anyway
                ESP_LOGE(TAG, "RX callback is NULL!");
            }

        } else if (len < 0) {
            ESP_LOGE(TAG, "UART%d read error", s_uart_config.port);
            // Handle error, maybe break loop or add delay?
        }
        // else len == 0 (timeout), just loop again

        // Small delay to prevent busy-waiting if no data comes frequently
        vTaskDelay(pdMS_TO_TICKS(10));
    }

    // Should not be reached unless loop is broken
    free(rx_buffer);
    ESP_LOGW(TAG, "UART RX task exiting for UART%d.", s_uart_config.port);
    s_uart_rx_task_handle = NULL; // Mark task as gone
    vTaskDelete(NULL);
}
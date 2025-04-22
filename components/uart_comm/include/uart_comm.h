// components/uart_comm/include/uart_comm.h
#ifndef UART_COMM_H
#define UART_COMM_H

#include "esp_err.h"
#include "driver/uart.h"
#include <stddef.h> // For size_t
#include <stdint.h> // For uint8_t

/**
 * @brief UART communication configuration structure.
 */
typedef struct {
    uart_port_t port;           /*!< UART port number */
    int tx_pin;                 /*!< UART TX GPIO number */
    int rx_pin;                 /*!< UART RX GPIO number */
    int baud_rate;              /*!< UART baud rate */
    int rx_buffer_size;         /*!< UART RX ring buffer size */
    int tx_buffer_size;         /*!< UART TX ring buffer size (0 for default/no buffer) */
    int queue_size;             /*!< UART event queue size (0 for default) */
} uart_comm_config_t;

/**
 * @brief Callback function type for received UART data.
 *
 * This function is declared as __weak, allowing the main application to
 * provide its own implementation. It will be called by the UART component
 * when data is received.
 *
 * @param data Pointer to the received data buffer.
 * @param len Length of the received data.
 */
typedef void (*uart_comm_rx_callback_t)(const uint8_t *data, size_t len);

/**
 * @brief Initializes the UART communication component.
 *
 * Configures the UART driver, installs it, and starts the RX task.
 *
 * @param config Pointer to the UART configuration structure.
 * @param rx_callback Pointer to the callback function to be called when data is received.
 *                    The application *must* provide this callback.
 * @return esp_err_t ESP_OK on success, or an error code.
 */
esp_err_t uart_comm_init(const uart_comm_config_t *config, uart_comm_rx_callback_t rx_callback);

/**
 * @brief Transmits data over UART.
 *
 * This function is thread-safe.
 *
 * @param data Pointer to the data buffer to send.
 * @param len Length of the data to send.
 * @return esp_err_t ESP_OK on success, ESP_FAIL if UART not initialized or write fails,
 *         ESP_ERR_INVALID_ARG if arguments are invalid.
 */
esp_err_t uart_comm_transmit(const uint8_t *data, size_t len);

/**
 * @brief Deinitializes the UART communication component.
 *
 * Stops the RX task and uninstalls the UART driver.
 *
 * @return esp_err_t ESP_OK on success, or an error code.
 */
esp_err_t uart_comm_deinit(void);

#endif // UART_COMM_H
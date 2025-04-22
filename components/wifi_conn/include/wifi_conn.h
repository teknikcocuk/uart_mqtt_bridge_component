// components/wifi_conn/include/wifi_conn.h
#ifndef WIFI_CONN_H
#define WIFI_CONN_H

#include "esp_err.h"
#include "esp_netif_types.h" // For esp_netif_ip_info_t

/**
 * @brief WiFi connection status enumeration.
 */
typedef enum {
    WIFI_CONN_STATUS_DISCONNECTED,
    WIFI_CONN_STATUS_CONNECTING,
    WIFI_CONN_STATUS_CONNECTED_GOT_IP,
    WIFI_CONN_STATUS_CONNECTION_FAILED // Added for explicit failure signal
} wifi_conn_status_t;

/**
 * @brief Callback function type for WiFi status changes.
 *
 * @param status The new WiFi connection status.
 * @param ip_info Pointer to IP information if status is WIFI_CONN_STATUS_CONNECTED_GOT_IP, NULL otherwise.
 */
typedef void (*wifi_conn_status_callback_t)(wifi_conn_status_t status, const esp_netif_ip_info_t *ip_info);

/**
 * @brief Initializes the WiFi connection component in Station mode.
 *
 * Initializes TCP/IP stack, event loop (if not already done), WiFi stack,
 * and starts persistent connection attempts to the specified AP.
 * Requires NVS flash to be initialized beforehand by the main application.
 * Requires default event loop and netif to be created beforehand by the main application.
 *
 * @param ssid The SSID of the target Access Point.
 * @param password The password of the target Access Point.
 * @param status_cb Pointer to the callback function to be called on status changes.
 *                  The application *must* provide this callback.
 * @return esp_err_t ESP_OK on successful initiation, or an error code.
 */
esp_err_t wifi_conn_init_sta(const char *ssid, const char *password, wifi_conn_status_callback_t status_cb);

/**
 * @brief Checks if the WiFi is currently connected (has an IP address).
 *
 * @return true if connected, false otherwise.
 */
bool wifi_conn_is_connected(void);

/**
 * @brief Deinitializes the WiFi connection component.
 *
 * Stops WiFi, unregisters event handlers, and deinitializes the WiFi stack.
 *
 * @return esp_err_t ESP_OK on success, or an error code.
 */
esp_err_t wifi_conn_deinit(void);

#endif // WIFI_CONN_H
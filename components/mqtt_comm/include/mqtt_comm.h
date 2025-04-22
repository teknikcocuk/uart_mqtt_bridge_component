// components/mqtt_comm/include/mqtt_comm.h
#ifndef MQTT_COMM_H
#define MQTT_COMM_H

#include "esp_err.h"
#include <stddef.h> // For size_t

/**
 * @brief MQTT communication configuration structure.
 */
typedef struct {
    const char *broker_uri;     /*!< Full MQTT broker URI (e.g., "mqtt://host.com:1883") */
    const char *client_id;      /*!< MQTT client ID (NULL for default based on MAC) */
    const char *username;       /*!< MQTT username (NULL if no authentication) */
    const char *password;       /*!< MQTT password (NULL if no authentication) */
    // Add LWT parameters if needed:
    // const char *lwt_topic;
    // const char *lwt_msg;
    // int lwt_qos;
    // int lwt_retain;
} mqtt_comm_config_t;

/**
 * @brief MQTT connection status enumeration.
 */
typedef enum {
    MQTT_CONN_STATUS_DISCONNECTED,
    MQTT_CONN_STATUS_CONNECTING, // Note: ESP-IDF MQTT client handles this internally
    MQTT_CONN_STATUS_CONNECTED,
    MQTT_CONN_STATUS_ERROR
} mqtt_conn_status_t;

/**
 * @brief Callback function type for MQTT status changes.
 *
 * @param status The new MQTT connection status.
 */
typedef void (*mqtt_conn_status_callback_t)(mqtt_conn_status_t status);

/**
 * @brief Callback function type for received MQTT messages.
 *
 * @param topic Pointer to the topic string.
 * @param topic_len Length of the topic string.
 * @param data Pointer to the payload data.
 * @param data_len Length of the payload data.
 */
typedef void (*mqtt_comm_data_callback_t)(const char *topic, size_t topic_len, const char *data, size_t data_len);

/**
 * @brief Initializes the MQTT communication component.
 *
 * Configures and starts the ESP-IDF MQTT client. It will automatically
 * attempt to connect when a network connection (WiFi) is available.
 * Assumes WiFi component is initialized and managing the connection.
 *
 * @param config Pointer to the MQTT configuration structure.
 * @param status_cb Pointer to the callback function for connection status changes.
 * @param data_cb Pointer to the callback function for incoming subscribed messages.
 * @return esp_err_t ESP_OK on success, or an error code if client init fails.
 */
esp_err_t mqtt_comm_init(const mqtt_comm_config_t *config,
                         mqtt_conn_status_callback_t status_cb,
                         mqtt_comm_data_callback_t data_cb);

/**
 * @brief Publishes a message to an MQTT topic.
 *
 * This function is thread-safe. It will return an error if the client
 * is not currently connected.
 *
 * @param topic The topic string to publish to.
 * @param data Pointer to the payload data.
 * @param len Length of the payload data. Set to 0 for an empty payload.
 *            Use -1 for len if `data` is a null-terminated string (strlen will be used).
 * @param qos QoS level (0, 1, or 2).
 * @param retain Retain flag (0 or 1).
 * @return esp_err_t ESP_OK on successful queuing of the publish message,
 *         ESP_FAIL if client not connected or publish fails,
 *         ESP_ERR_INVALID_ARG if arguments are invalid.
 */
esp_err_t mqtt_comm_publish(const char *topic, const char *data, int len, int qos, int retain);

/**
 * @brief Subscribes to an MQTT topic.
 *
 * This function is thread-safe. It will return an error if the client
 * is not currently connected.
 *
 * @param topic The topic string to subscribe to.
 * @param qos The maximum QoS level for messages received on this topic.
 * @return esp_err_t ESP_OK on successful queuing of the subscribe message,
 *         ESP_FAIL if client not connected or subscribe fails,
 *         ESP_ERR_INVALID_ARG if arguments are invalid.
 */
esp_err_t mqtt_comm_subscribe(const char *topic, int qos);

/**
 * @brief Unsubscribes from an MQTT topic.
 *
 * This function is thread-safe. It will return an error if the client
 * is not currently connected.
 *
 * @param topic The topic string to unsubscribe from.
 * @return esp_err_t ESP_OK on successful queuing of the unsubscribe message,
 *         ESP_FAIL if client not connected or unsubscribe fails,
 *         ESP_ERR_INVALID_ARG if arguments are invalid.
 */
esp_err_t mqtt_comm_unsubscribe(const char *topic);

/**
 * @brief Checks if the MQTT client is currently connected to the broker.
 *
 * @return true if connected, false otherwise.
 */
bool mqtt_comm_is_connected(void);

/**
 * @brief Deinitializes the MQTT communication component.
 *
 * Stops and destroys the MQTT client.
 *
 * @return esp_err_t ESP_OK on success, or an error code.
 */
esp_err_t mqtt_comm_deinit(void);

#endif // MQTT_COMM_H
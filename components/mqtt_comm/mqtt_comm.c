// components/mqtt_comm/mqtt_comm.c
#include <string.h>
#include <stdlib.h> // For malloc if default client ID needed
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_wifi.h" // For MAC address -> client ID
#include "mqtt_client.h"
#include "mqtt_comm.h" // Include own header

static const char *TAG = "MQTT_COMM";

// State variables
static esp_mqtt_client_handle_t s_client = NULL;
static mqtt_conn_status_callback_t s_status_callback = NULL;
static mqtt_comm_data_callback_t s_data_callback = NULL;
static SemaphoreHandle_t s_client_mutex = NULL; // Protects s_client handle and s_is_connected
static volatile bool s_is_connected = false;
static bool s_is_initialized = false; // Tracks if init was called successfully
static char* s_default_client_id = NULL; // Store generated client ID if needed

// Forward declaration
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);

// Helper to generate default client ID from MAC
static char* generate_default_client_id() {
    uint8_t mac[6];
    esp_wifi_get_mac(WIFI_IF_STA, mac); // Assumes STA interface is up for MAC
    char *client_id = malloc(19); // "ESP32_XXYYZZ" + padding/null
    if (client_id) {
        snprintf(client_id, 19, "ESP32_%02X%02X%02X", mac[3], mac[4], mac[5]);
    }
    return client_id;
}


esp_err_t mqtt_comm_init(const mqtt_comm_config_t *config,
                         mqtt_conn_status_callback_t status_cb,
                         mqtt_comm_data_callback_t data_cb) {
    if (s_is_initialized) {
        ESP_LOGW(TAG, "MQTT already initialized.");
        return ESP_OK;
    }
    if (!config || !config->broker_uri || !status_cb || !data_cb) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing MQTT client...");
    s_status_callback = status_cb;
    s_data_callback = data_cb;

    s_client_mutex = xSemaphoreCreateMutex();
    if (s_client_mutex == NULL) {
        ESP_LOGE(TAG, "Failed to create client mutex");
        return ESP_FAIL;
    }

    const char* client_id_to_use = config->client_id;
    if (!client_id_to_use) {
        s_default_client_id = generate_default_client_id();
        if (!s_default_client_id) {
             ESP_LOGE(TAG, "Failed to generate default client ID");
             vSemaphoreDelete(s_client_mutex);
             s_client_mutex = NULL;
             return ESP_FAIL;
        }
        client_id_to_use = s_default_client_id;
        ESP_LOGI(TAG, "Using generated Client ID: %s", client_id_to_use);
    }

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = config->broker_uri,
        .credentials.client_id = client_id_to_use,
        .credentials.username = config->username,
        .credentials.authentication.password = config->password,
        // Add LWT config here if needed from config struct
    };

    s_client = esp_mqtt_client_init(&mqtt_cfg);
    if (s_client == NULL) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client");
        if (s_default_client_id) {
            free(s_default_client_id);
            s_default_client_id = NULL;
        }
        vSemaphoreDelete(s_client_mutex);
        s_client_mutex = NULL;
        return ESP_FAIL;
    }

    esp_err_t ret = esp_mqtt_client_register_event(s_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(ret));
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
         if (s_default_client_id) {
            free(s_default_client_id);
            s_default_client_id = NULL;
        }
        vSemaphoreDelete(s_client_mutex);
        s_client_mutex = NULL;
        return ret;
    }

    ret = esp_mqtt_client_start(s_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
        // No need to unregister handler, destroy cleans up
        esp_mqtt_client_destroy(s_client);
        s_client = NULL;
         if (s_default_client_id) {
            free(s_default_client_id);
            s_default_client_id = NULL;
        }
        vSemaphoreDelete(s_client_mutex);
        s_client_mutex = NULL;
        return ret;
    }

    s_is_initialized = true;
    ESP_LOGI(TAG, "MQTT client initialization finished and started.");
    return ESP_OK;
}

esp_err_t mqtt_comm_publish(const char *topic, const char *data, int len, int qos, int retain) {
    if (!s_is_initialized || !topic || (!data && len != 0)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_FAIL;
    if (xSemaphoreTake(s_client_mutex, pdMS_TO_TICKS(100)) == pdTRUE) { // Wait briefly
        if (s_is_connected && s_client) {
            int msg_id = esp_mqtt_client_publish(s_client, topic, data, len, qos, retain);
            if (msg_id != -1) {
                ESP_LOGD(TAG, "Publish queued successfully to topic '%s', msg_id=%d", topic, msg_id);
                result = ESP_OK;
            } else {
                ESP_LOGE(TAG, "Failed to queue publish message to topic '%s'", topic);
                result = ESP_FAIL;
            }
        } else {
            ESP_LOGW(TAG, "MQTT not connected, cannot publish to topic '%s'", topic);
            result = ESP_FAIL; // Indicate not connected
        }
        xSemaphoreGive(s_client_mutex);
    } else {
        ESP_LOGE(TAG, "Could not obtain MQTT client mutex for publish.");
        result = ESP_FAIL; // Or maybe ESP_ERR_TIMEOUT
    }
    return result;
}

esp_err_t mqtt_comm_subscribe(const char *topic, int qos) {
    if (!s_is_initialized || !topic) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_FAIL;
     if (xSemaphoreTake(s_client_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_is_connected && s_client) {
            int msg_id = esp_mqtt_client_subscribe(s_client, topic, qos);
             if (msg_id != -1) {
                ESP_LOGI(TAG, "Subscribe queued successfully for topic '%s', msg_id=%d", topic, msg_id);
                result = ESP_OK;
            } else {
                ESP_LOGE(TAG, "Failed to queue subscribe message for topic '%s'", topic);
                result = ESP_FAIL;
            }
        } else {
             ESP_LOGW(TAG, "MQTT not connected, cannot subscribe to topic '%s'", topic);
            result = ESP_FAIL;
        }
        xSemaphoreGive(s_client_mutex);
    } else {
        ESP_LOGE(TAG, "Could not obtain MQTT client mutex for subscribe.");
        result = ESP_FAIL;
    }
    return result;
}

esp_err_t mqtt_comm_unsubscribe(const char *topic) {
     if (!s_is_initialized || !topic) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t result = ESP_FAIL;
     if (xSemaphoreTake(s_client_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
        if (s_is_connected && s_client) {
            int msg_id = esp_mqtt_client_unsubscribe(s_client, topic);
             if (msg_id != -1) {
                ESP_LOGI(TAG, "Unsubscribe queued successfully for topic '%s', msg_id=%d", topic, msg_id);
                result = ESP_OK;
            } else {
                ESP_LOGE(TAG, "Failed to queue unsubscribe message for topic '%s'", topic);
                result = ESP_FAIL;
            }
        } else {
            ESP_LOGW(TAG, "MQTT not connected, cannot unsubscribe from topic '%s'", topic);
            result = ESP_FAIL;
        }
        xSemaphoreGive(s_client_mutex);
    } else {
        ESP_LOGE(TAG, "Could not obtain MQTT client mutex for unsubscribe.");
        result = ESP_FAIL;
    }
    return result;
}

bool mqtt_comm_is_connected(void) {
    // Reading volatile bool is generally atomic, but mutex ensures consistency
    // if read happens during a state change in the event handler.
    bool connected = false;
    if (xSemaphoreTake(s_client_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        connected = s_is_connected;
        xSemaphoreGive(s_client_mutex);
    } else {
         ESP_LOGW(TAG, "Could not obtain MQTT client mutex for is_connected check.");
    }
    return connected;
}


esp_err_t mqtt_comm_deinit(void) {
     if (!s_is_initialized) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Deinitializing MQTT client...");
    esp_err_t ret = ESP_OK;

    if (xSemaphoreTake(s_client_mutex, pdMS_TO_TICKS(500)) == pdTRUE) { // Wait longer for mutex during deinit
        if (s_client) {
            ret = esp_mqtt_client_stop(s_client); // Stop first
            if (ret != ESP_OK) ESP_LOGE(TAG, "esp_mqtt_client_stop failed: %s", esp_err_to_name(ret));
            ret = esp_mqtt_client_destroy(s_client);
             if (ret != ESP_OK) ESP_LOGE(TAG, "esp_mqtt_client_destroy failed: %s", esp_err_to_name(ret));
            s_client = NULL;
            s_is_connected = false;
        }
        s_is_initialized = false; // Mark as deinitialized inside mutex
        xSemaphoreGive(s_client_mutex);
    } else {
         ESP_LOGE(TAG, "Could not obtain MQTT client mutex for deinit.");
         // Can't safely destroy client if mutex not obtained
         ret = ESP_FAIL;
    }


    if (s_client_mutex) {
        vSemaphoreDelete(s_client_mutex);
        s_client_mutex = NULL;
    }
     if (s_default_client_id) {
        free(s_default_client_id);
        s_default_client_id = NULL;
    }

    s_status_callback = NULL;
    s_data_callback = NULL;

    ESP_LOGI(TAG, "MQTT client deinitialized.");
    return ret;
}


// --- Internal Event Handler ---

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client; // Can use this or s_client

    switch ((esp_mqtt_event_id_t)event_id) {
        case MQTT_EVENT_BEFORE_CONNECT:
             ESP_LOGI(TAG, "MQTT_EVENT_BEFORE_CONNECT");
             // Could set status to connecting here if desired
             break;
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            if (xSemaphoreTake(s_client_mutex, portMAX_DELAY) == pdTRUE) {
                s_is_connected = true;
                xSemaphoreGive(s_client_mutex);
            }
            if (s_status_callback) s_status_callback(MQTT_CONN_STATUS_CONNECTED);
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT_EVENT_DISCONNECTED");
             if (xSemaphoreTake(s_client_mutex, portMAX_DELAY) == pdTRUE) {
                s_is_connected = false;
                 xSemaphoreGive(s_client_mutex);
            }
            if (s_status_callback) s_status_callback(MQTT_CONN_STATUS_DISCONNECTED);
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGI(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            ESP_LOGD(TAG, "TOPIC=%.*s", event->topic_len, event->topic);
            ESP_LOGD(TAG, "DATA=%.*s", event->data_len, event->data);
            if (s_data_callback) {
                s_data_callback(event->topic, event->topic_len, event->data, event->data_len);
            }
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle) {
                 ESP_LOGE(TAG, "Last error code: 0x%x", event->error_handle->error_type);
                 ESP_LOGE(TAG, "Last error step: %d", event->error_handle->connect_return_code);
                 // Check specific error codes if needed
            }
             if (xSemaphoreTake(s_client_mutex, portMAX_DELAY) == pdTRUE) {
                s_is_connected = false; // Assume disconnect on error
                 xSemaphoreGive(s_client_mutex);
            }
            if (s_status_callback) s_status_callback(MQTT_CONN_STATUS_ERROR);
            break;
        default:
            ESP_LOGD(TAG, "Other MQTT event id: %d", event->event_id);
            break;
    }
}
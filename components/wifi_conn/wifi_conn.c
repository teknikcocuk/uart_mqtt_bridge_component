// components/wifi_conn/wifi_conn.c
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h" // For vTaskDelay
#include "freertos/event_groups.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h" // Required for IP info

#include "wifi_conn.h" // Include own header

#define WIFI_CONN_MAX_RETRY 10 // Example: Limit explicit retries before pausing longer
#define WIFI_CONN_RETRY_DELAY_MS 5000 // Delay between retries

static const char *TAG = "WIFI_CONN";

// State variables
static EventGroupHandle_t s_wifi_event_group = NULL;
static wifi_conn_status_callback_t s_status_callback = NULL;
static bool s_wifi_initialized = false;
static bool s_wifi_started = false;
static int s_retry_num = 0;

// Event bits
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1 // No longer means permanent fail, just failed connection attempt

// Forward declarations
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data);
static void ip_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data);

esp_err_t wifi_conn_init_sta(const char *ssid, const char *password, wifi_conn_status_callback_t status_cb) {
    if (s_wifi_initialized) {
        ESP_LOGW(TAG, "WiFi already initialized.");
        return ESP_OK;
    }
    if (!ssid || !password || !status_cb) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing WiFi STA mode...");
    s_status_callback = status_cb;

    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        return ESP_FAIL;
    }

    // NOTE: Assumes esp_netif_init() and esp_event_loop_create_default()
    // have been called in the main application.
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
     if (sta_netif == NULL) {
        ESP_LOGE(TAG, "Failed to create default STA netif");
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
        return ESP_FAIL;
    }


    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(ret));
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
        esp_netif_destroy(sta_netif); // Clean up netif
        return ret;
    }

    // Register event handlers
    ret = esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL);
    if (ret != ESP_OK) goto cleanup;
    ret = esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler, NULL);
    if (ret != ESP_OK) goto cleanup_wifi_handler;


    wifi_config_t wifi_config = {
        .sta = {
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // Or adjust as needed
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
    wifi_config.sta.ssid[sizeof(wifi_config.sta.ssid) - 1] = '\0'; // Ensure null termination
    strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
    wifi_config.sta.password[sizeof(wifi_config.sta.password) - 1] = '\0'; // Ensure null termination

    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) goto cleanup_ip_handler;
    ret = esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    if (ret != ESP_OK) goto cleanup_ip_handler;
    ret = esp_wifi_start();
    if (ret != ESP_OK) goto cleanup_ip_handler;

    s_wifi_started = true;
    s_wifi_initialized = true;
    ESP_LOGI(TAG, "WiFi STA initialization finished. Connection attempts starting.");
    return ESP_OK;

// Cleanup labels
cleanup_ip_handler:
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler);
cleanup_wifi_handler:
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);
cleanup:
    esp_wifi_deinit(); // Deinit wifi stack
    esp_netif_destroy(sta_netif); // Clean up netif
    vEventGroupDelete(s_wifi_event_group);
    s_wifi_event_group = NULL;
    ESP_LOGE(TAG, "WiFi STA initialization failed during setup: %s", esp_err_to_name(ret));
    return ret;

}

bool wifi_conn_is_connected(void) {
    if (!s_wifi_event_group) return false;
    return (xEventGroupGetBits(s_wifi_event_group) & WIFI_CONNECTED_BIT) != 0;
}

esp_err_t wifi_conn_deinit(void) {
    if (!s_wifi_initialized) {
        return ESP_OK;
    }
    ESP_LOGI(TAG, "Deinitializing WiFi...");

    esp_err_t ret = ESP_OK;

    // Unregister handlers first
    esp_event_handler_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, &ip_event_handler);
    esp_event_handler_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler);

    if (s_wifi_started) {
        ret = esp_wifi_stop();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_stop failed: %s", esp_err_to_name(ret));
            // Continue deinit
        }
        s_wifi_started = false;
    }

    ret = esp_wifi_deinit();
     if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_deinit failed: %s", esp_err_to_name(ret));
        // Continue deinit
    }

    esp_netif_t *sta_netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (sta_netif) {
        esp_netif_destroy(sta_netif);
    }


    if (s_wifi_event_group) {
        vEventGroupDelete(s_wifi_event_group);
        s_wifi_event_group = NULL;
    }

    s_wifi_initialized = false;
    s_status_callback = NULL;
    s_retry_num = 0;

    ESP_LOGI(TAG, "WiFi Deinitialized.");
    return ret; // Return the last significant error code
}

// --- Internal Event Handlers ---

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data)
{
    if (event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WIFI_EVENT_STA_START received, attempting to connect...");
        if (s_status_callback) s_status_callback(WIFI_CONN_STATUS_CONNECTING, NULL);
        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK) {
             ESP_LOGE(TAG, "esp_wifi_connect failed on start: %s", esp_err_to_name(ret));
             // Maybe notify failure? Or let disconnect event handle it.
             if (s_status_callback) s_status_callback(WIFI_CONN_STATUS_CONNECTION_FAILED, NULL);
        }
    } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGW(TAG, "WIFI_EVENT_STA_DISCONNECTED received.");
        s_retry_num++;
        // Clear connected bit
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        // Notify application
        if (s_status_callback) s_status_callback(WIFI_CONN_STATUS_DISCONNECTED, NULL);

        // Persistent Retry Logic
        ESP_LOGI(TAG, "Retrying connection (attempt %d)...", s_retry_num);
        if (s_status_callback) s_status_callback(WIFI_CONN_STATUS_CONNECTING, NULL); // Notify that we are trying again
        vTaskDelay(pdMS_TO_TICKS(WIFI_CONN_RETRY_DELAY_MS));
        esp_err_t ret = esp_wifi_connect();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "esp_wifi_connect failed on retry: %s", esp_err_to_name(ret));
            // If connect itself fails immediately, maybe signal failure?
             if (s_status_callback) s_status_callback(WIFI_CONN_STATUS_CONNECTION_FAILED, NULL);
        }
    }
}

static void ip_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data)
{
    if (event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "IP_EVENT_STA_GOT_IP received: " IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0; // Reset retry counter on success
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        // Notify application
        if (s_status_callback) s_status_callback(WIFI_CONN_STATUS_CONNECTED_GOT_IP, &event->ip_info);
    }
}
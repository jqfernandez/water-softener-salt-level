/* Water Softener Salt Level Monitor
 *
 * This application monitors the salt level in a water softener tank using
 * an HC-SR04 ultrasonic sensor and publishes the data to Home Assistant via MQTT.
 */

#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"

static const char *TAG = "SALT_LEVEL";

/* FreeRTOS event group to signal when we are connected*/
static EventGroupHandle_t s_wifi_event_group;

/* The event group allows multiple bits for each event, but we only care about two events:
 * - we are connected to the AP with an IP
 * - we failed to connect after the maximum amount of retries */
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

static int s_retry_num = 0;
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;

/* Wi-Fi event handler */
static void event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < CONFIG_WIFI_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG, "Retry to connect to the AP");
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
        }
        ESP_LOGI(TAG,"Connect to the AP fail");
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

/* Initialize Wi-Fi in station mode */
void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = CONFIG_WIFI_SSID,
            .password = CONFIG_WIFI_PASSWORD,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG, "wifi_init_sta finished.");

    /* Waiting until either the connection is established (WIFI_CONNECTED_BIT) or connection failed for the maximum
     * number of re-tries (WIFI_FAIL_BIT). The bits are set by event_handler() (see above) */
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE,
            pdFALSE,
            portMAX_DELAY);

    /* xEventGroupWaitBits() returns the bits before the call returned, hence we can test which event actually
     * happened. */
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to AP SSID:%s", CONFIG_WIFI_SSID);
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGI(TAG, "Failed to connect to SSID:%s", CONFIG_WIFI_SSID);
    } else {
        ESP_LOGE(TAG, "UNEXPECTED EVENT");
    }
}

/* MQTT event handler */
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
        mqtt_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
        mqtt_connected = false;
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGE(TAG, "Last error code: 0x%x", event->error_handle->esp_transport_sock_errno);
        } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGE(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
        }
        break;
    default:
        break;
    }
}

/* Initialize MQTT client */
static void mqtt_app_start(void)
{
    ESP_LOGI(TAG, "=== MQTT Configuration ===");
    ESP_LOGI(TAG, "Broker URL: %s", CONFIG_MQTT_BROKER_URL);
    ESP_LOGI(TAG, "Client ID: %s", CONFIG_MQTT_CLIENT_ID);
    ESP_LOGI(TAG, "Username: '%s' (length: %d)", CONFIG_MQTT_USERNAME, strlen(CONFIG_MQTT_USERNAME));
    ESP_LOGI(TAG, "Password length: %d", strlen(CONFIG_MQTT_PASSWORD));
    ESP_LOGI(TAG, "========================");

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_MQTT_BROKER_URL,
        .credentials = {
            .client_id = CONFIG_MQTT_CLIENT_ID,
        },
    };

    // Only set username/password if they are not empty
    if (strlen(CONFIG_MQTT_USERNAME) > 0) {
        mqtt_cfg.credentials.username = CONFIG_MQTT_USERNAME;
    } else {
        ESP_LOGW(TAG, "No MQTT username configured");
    }

    if (strlen(CONFIG_MQTT_PASSWORD) > 0) {
        mqtt_cfg.credentials.authentication.password = CONFIG_MQTT_PASSWORD;
    } else {
        ESP_LOGW(TAG, "No MQTT password configured");
    }

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    ESP_LOGI(TAG, "MQTT client started");
}

/* Publish Home Assistant MQTT Discovery configuration */
static void publish_ha_discovery(void)
{
    if (!mqtt_connected) {
        ESP_LOGW(TAG, "MQTT not connected, skipping discovery");
        return;
    }

    char config_topic[128];
    char config_payload[512];

    // Discovery topic for distance sensor
    snprintf(config_topic, sizeof(config_topic),
             "homeassistant/sensor/%s/distance/config", CONFIG_MQTT_CLIENT_ID);

    snprintf(config_payload, sizeof(config_payload),
             "{\"name\":\"Salt Level Distance\","
             "\"state_topic\":\"homeassistant/sensor/%s/state\","
             "\"unit_of_measurement\":\"cm\","
             "\"value_template\":\"{{ value_json.distance }}\","
             "\"unique_id\":\"%s_distance\","
             "\"device\":{\"identifiers\":[\"%s\"],"
             "\"name\":\"Water Softener Salt Level\","
             "\"model\":\"ESP32 HC-SR04\","
             "\"manufacturer\":\"DIY\"}}",
             CONFIG_MQTT_CLIENT_ID, CONFIG_MQTT_CLIENT_ID, CONFIG_MQTT_CLIENT_ID);

    esp_mqtt_client_publish(mqtt_client, config_topic, config_payload, 0, 1, true);
    ESP_LOGI(TAG, "Published distance sensor discovery");

    // Discovery topic for percentage sensor
    snprintf(config_topic, sizeof(config_topic),
             "homeassistant/sensor/%s/percentage/config", CONFIG_MQTT_CLIENT_ID);

    snprintf(config_payload, sizeof(config_payload),
             "{\"name\":\"Salt Level Percentage\","
             "\"state_topic\":\"homeassistant/sensor/%s/state\","
             "\"unit_of_measurement\":\"%%\","
             "\"value_template\":\"{{ value_json.percentage }}\","
             "\"unique_id\":\"%s_percentage\","
             "\"device\":{\"identifiers\":[\"%s\"]}}",
             CONFIG_MQTT_CLIENT_ID, CONFIG_MQTT_CLIENT_ID, CONFIG_MQTT_CLIENT_ID);

    esp_mqtt_client_publish(mqtt_client, config_topic, config_payload, 0, 1, true);
    ESP_LOGI(TAG, "Published percentage sensor discovery");
}

/* Mock sensor reading (replace with real HC-SR04 code later) */
static float read_distance_cm(void)
{
    // Generate random distance between 10 and tank height
    // This simulates the sensor reading from top of tank down to salt
    static int count = 0;
    count++;

    // Simulate salt level going down over time
    float distance = 10.0f + (count % 50) * 1.5f;

    ESP_LOGI(TAG, "Mock sensor reading: %.1f cm", distance);
    return distance;
}

/* Calculate salt level percentage from distance */
static float calculate_percentage(float distance_cm)
{
    float tank_height = (float)CONFIG_TANK_HEIGHT_CM;

    // Distance is measured from top, so we need to invert it
    // If distance is small (near top), tank is nearly full
    // If distance is large (near bottom), tank is nearly empty
    float salt_height = tank_height - distance_cm;
    float percentage = (salt_height / tank_height) * 100.0f;

    // Clamp between 0 and 100
    if (percentage < 0) percentage = 0;
    if (percentage > 100) percentage = 100;

    return percentage;
}

/* Main sensor reading and publishing task */
static void sensor_task(void *pvParameters)
{
    // Wait for MQTT to connect
    ESP_LOGI(TAG, "Waiting for MQTT connection...");
    while (!mqtt_connected) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    // Wait a bit more to ensure connection is stable
    vTaskDelay(pdMS_TO_TICKS(2000));

    // Publish discovery messages
    ESP_LOGI(TAG, "Publishing Home Assistant discovery messages...");
    publish_ha_discovery();
    ESP_LOGI(TAG, "Discovery messages sent!");

    char state_topic[128];
    char payload[256];

    snprintf(state_topic, sizeof(state_topic),
             "homeassistant/sensor/%s/state", CONFIG_MQTT_CLIENT_ID);

    while (1) {
        // Read sensor
        float distance = read_distance_cm();
        float percentage = calculate_percentage(distance);

        ESP_LOGI(TAG, "Distance: %.1f cm, Salt level: %.1f%%", distance, percentage);

        // Publish to MQTT
        if (mqtt_connected) {
            snprintf(payload, sizeof(payload),
                     "{\"distance\":%.1f,\"percentage\":%.1f}",
                     distance, percentage);

            int msg_id = esp_mqtt_client_publish(mqtt_client, state_topic, payload, 0, 0, false);
            ESP_LOGI(TAG, "Published to MQTT, msg_id=%d", msg_id);
        } else {
            ESP_LOGW(TAG, "MQTT not connected, skipping publish");
        }

        // Wait for next reading
        vTaskDelay(pdMS_TO_TICKS(CONFIG_READING_INTERVAL_SEC * 1000));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Water Softener Salt Level Monitor starting...");

    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // Initialize Wi-Fi
    ESP_LOGI(TAG, "Connecting to Wi-Fi...");
    wifi_init_sta();

    // Initialize MQTT
    ESP_LOGI(TAG, "Starting MQTT client...");
    mqtt_app_start();

    // Create sensor reading task
    xTaskCreate(sensor_task, "sensor_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "Initialization complete");
}

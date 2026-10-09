#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdkconfig.h"
#include "driver/gpio.h"
#include "esp_crt_bundle.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_netif_sntp.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mqtt_client.h"
#include "nvs_flash.h"
#include "button_debounce.h"
#include "ha_discovery.h"

#if !CONFIG_IDF_TARGET_ESP32
#error "This firmware targets the original ESP32, including ESP32-WROOM-32D"
#endif

static const char *TAG = "mqtt_buttons";
static const gpio_num_t button_gpios[] = {
    CONFIG_BUTTON_1_GPIO,
#if CONFIG_BUTTON_COUNT >= 2
    CONFIG_BUTTON_2_GPIO,
#endif
#if CONFIG_BUTTON_COUNT >= 3
    CONFIG_BUTTON_3_GPIO,
#endif
#if CONFIG_BUTTON_COUNT >= 4
    CONFIG_BUTTON_4_GPIO,
#endif
};
static button_debounce_t buttons[CONFIG_BUTTON_COUNT];
static atomic_bool button_states[CONFIG_BUTTON_COUNT];
static atomic_bool wifi_connected;
static atomic_bool mqtt_connected;
static atomic_bool discovery_pending;
static ha_device_t device;
static esp_mqtt_client_handle_t mqtt;
static TaskHandle_t publisher_handle;

#ifdef CONFIG_BUTTON_MQTT_CUSTOM_CA
extern const char broker_ca_start[] asm("_binary_broker_ca_pem_start");
#endif

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static bool read_pressed(unsigned index)
{
#ifdef CONFIG_BUTTON_ACTIVE_HIGH
    return gpio_get_level(button_gpios[index]) != 0;
#else
    return gpio_get_level(button_gpios[index]) == 0;
#endif
}

static void button_task(void *arg)
{
    (void)arg;
    for (;;) {
        uint32_t now = now_ms();
        for (unsigned i = 0; i < CONFIG_BUTTON_COUNT; ++i) {
            if (button_debounce_update(&buttons[i], read_pressed(i), now, CONFIG_BUTTON_DEBOUNCE_MS)) {
                atomic_store(&button_states[i], buttons[i].stable);
                ESP_LOGI(TAG, "Button %u: %s", i + 1, buttons[i].stable ? "pressed" : "released");
                if (publisher_handle) {
                    xTaskNotifyGive(publisher_handle);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* This worker may block on MQTT writes, but never blocks GPIO sampling.
 * Retained snapshots describe the current physical state. Offline transitions
 * coalesce, rather than replaying historical clicks after reconnection. */
static bool publish_retained(const char *topic, const char *payload)
{
    if (!atomic_load(&mqtt_connected) || !atomic_load(&wifi_connected)) {
        return false;
    }
    return esp_mqtt_client_publish(mqtt, topic, payload, 0, 1, 1) >= 0;
}

static bool publish_discovery(void)
{
    for (unsigned i = 0; i < CONFIG_BUTTON_COUNT; ++i) {
        char topic[100];
        char *json = ha_discovery_json(&device, i + 1);
        bool ok = json && ha_config_topic(&device, i + 1, topic, sizeof(topic)) &&
                  publish_retained(topic, json);
        free(json);
        if (!ok) {
            return false;
        }
    }
    /* Clear retained discovery for previously enabled extra buttons. */
    for (unsigned i = CONFIG_BUTTON_COUNT; i < 4; ++i) {
        char topic[100];
        if (!ha_config_topic(&device, i + 1, topic, sizeof(topic)) || !publish_retained(topic, "")) {
            return false;
        }
    }
    return true;
}

static void publisher_task(void *arg)
{
    (void)arg;
    bool published_states[CONFIG_BUTTON_COUNT] = {0};
    bool valid_states[CONFIG_BUTTON_COUNT] = {0};
    bool availability_pending = true;
    uint32_t last_refresh = now_ms();
    for (;;) {
        ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000));
        if (!atomic_load(&mqtt_connected) || !atomic_load(&wifi_connected)) {
            availability_pending = true;
            memset(valid_states, 0, sizeof(valid_states));
            continue;
        }
        bool announce = atomic_exchange(&discovery_pending, false);
        if (announce) {
            if (!publish_discovery()) {
                atomic_store(&discovery_pending, true);
            }
            memset(valid_states, 0, sizeof(valid_states));
            availability_pending = true;
        }
        uint32_t now = now_ms();
        if ((uint32_t)(now - last_refresh) >= 30000) {
            memset(valid_states, 0, sizeof(valid_states));
            availability_pending = true;
            last_refresh = now;
        }
        bool states_ok = true;
        for (unsigned i = 0; i < CONFIG_BUTTON_COUNT; ++i) {
            bool pressed = atomic_load(&button_states[i]);
            if (!valid_states[i] || published_states[i] != pressed) {
                char topic[80];
                if (ha_state_topic(&device, i + 1, topic, sizeof(topic)) &&
                    publish_retained(topic, pressed ? "ON" : "OFF")) {
                    published_states[i] = pressed;
                    valid_states[i] = true;
                } else {
                    valid_states[i] = false;
                }
            }
            states_ok &= valid_states[i];
        }
        /* Send online after the state snapshots so HA doesn't briefly expose
         * old retained button values on reconnection. */
        if (availability_pending && states_ok && publish_retained(device.availability_topic, "online")) {
            availability_pending = false;
        }
    }
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)arg;
    (void)base;
    esp_mqtt_event_handle_t event = event_data;
    switch (event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected; announcing Home Assistant button sensors");
        atomic_store(&mqtt_connected, true);
        atomic_store(&discovery_pending, true);
        if (esp_mqtt_client_subscribe(mqtt, "homeassistant/status", 1) < 0) {
            ESP_LOGW(TAG, "Home Assistant birth topic subscription failed");
        }
        xTaskNotifyGive(publisher_handle);
        break;
    case MQTT_EVENT_DISCONNECTED:
        atomic_store(&mqtt_connected, false);
        xTaskNotifyGive(publisher_handle);
        ESP_LOGW(TAG, "MQTT disconnected; automatic reconnect enabled");
        break;
    case MQTT_EVENT_DATA:
        if (event->topic_len == strlen("homeassistant/status") && event->topic &&
            memcmp(event->topic, "homeassistant/status", event->topic_len) == 0 &&
            event->current_data_offset == 0 && event->total_data_len == 6 &&
            event->data_len == 6 && memcmp(event->data, "online", 6) == 0) {
            atomic_store(&discovery_pending, true);
            xTaskNotifyGive(publisher_handle);
        }
        break;
    case MQTT_EVENT_ERROR:
        /* Do not print broker URI, Wi-Fi passwords, or MQTT credentials. */
        ESP_LOGW(TAG, "MQTT connection error; check broker access, credentials and TLS trust/time");
        break;
    default:
        break;
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)arg;
    if (base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&wifi_connected, false);
        wifi_event_sta_disconnected_t *event = event_data;
        ESP_LOGW(TAG, "Wi-Fi disconnected (reason %u); retrying", event->reason);
    } else if (base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        atomic_store(&wifi_connected, true);
        ESP_LOGI(TAG, "Wi-Fi connected with an IP address");
    }
}

static void wifi_retry_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (!atomic_load(&wifi_connected)) {
            esp_err_t error = esp_wifi_connect();
            if (error != ESP_OK) {
                ESP_LOGD(TAG, "Wi-Fi connect pending: %s", esp_err_to_name(error));
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

static void start_network(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(esp_netif_create_default_wifi_sta() ? ESP_OK : ESP_ERR_NO_MEM);
    wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&init));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));
    wifi_config_t wifi = {0};
    memcpy(wifi.sta.ssid, CONFIG_BUTTON_WIFI_SSID, strlen(CONFIG_BUTTON_WIFI_SSID));
    memcpy(wifi.sta.password, CONFIG_BUTTON_WIFI_PASSWORD, strlen(CONFIG_BUTTON_WIFI_PASSWORD));
    wifi.sta.threshold.authmode = strlen(CONFIG_BUTTON_WIFI_PASSWORD) ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_ps(WIFI_PS_NONE));
    ESP_ERROR_CHECK(xTaskCreate(wifi_retry_task, "wifi_retry", 3072, NULL, 3, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);

    bool tls = strncmp(CONFIG_BUTTON_MQTT_URI, "mqtts://", 8) == 0;
    if (tls) {
        esp_sntp_config_t sntp = ESP_NETIF_SNTP_DEFAULT_CONFIG(CONFIG_BUTTON_SNTP_SERVER);
        ESP_ERROR_CHECK(esp_netif_sntp_init(&sntp));
    }
    esp_mqtt_client_config_t config = {
        .broker.address.uri = CONFIG_BUTTON_MQTT_URI,
        .credentials.client_id = device.id,
        .credentials.username = strlen(CONFIG_BUTTON_MQTT_USERNAME) ? CONFIG_BUTTON_MQTT_USERNAME : NULL,
        .credentials.authentication.password = strlen(CONFIG_BUTTON_MQTT_PASSWORD) ? CONFIG_BUTTON_MQTT_PASSWORD : NULL,
        .session.keepalive = 30,
        .session.last_will = {
            .topic = device.availability_topic,
            .msg = "offline",
            .qos = 1,
            .retain = 1,
        },
        .network.reconnect_timeout_ms = 5000,
        .network.timeout_ms = 2000,
        .buffer.size = 2048,
    };
    if (tls) {
#ifdef CONFIG_BUTTON_MQTT_CUSTOM_CA
        config.broker.verification.certificate = broker_ca_start;
#else
        config.broker.verification.crt_bundle_attach = esp_crt_bundle_attach;
#endif
    }
    mqtt = esp_mqtt_client_init(&config);
    ESP_ERROR_CHECK(mqtt ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(xTaskCreate(publisher_task, "ha_publish", 6144, NULL, 4, &publisher_handle) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    ESP_ERROR_CHECK(esp_mqtt_client_start(mqtt));
}

void app_main(void)
{
    esp_err_t error = nvs_flash_init();
    if (error == ESP_ERR_NVS_NO_FREE_PAGES || error == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        error = nvs_flash_init();
    }
    ESP_ERROR_CHECK(error);
    uint64_t mask = 0;
    for (unsigned i = 0; i < CONFIG_BUTTON_COUNT; ++i) {
        gpio_num_t pin = button_gpios[i];
        /* GPIO6-11 are connected to flash on ESP32-WROOM-32D. GPIO34-39
         * lack the internal pull resistors used by this firmware. */
        ESP_ERROR_CHECK(GPIO_IS_VALID_GPIO(pin) && pin < 34 && !(pin >= 6 && pin <= 11)
                        ? ESP_OK : ESP_ERR_INVALID_ARG);
        uint64_t bit = 1ULL << pin;
        ESP_ERROR_CHECK((mask & bit) ? ESP_ERR_INVALID_ARG : ESP_OK);
        mask |= bit;
    }
    gpio_config_t input = {
        .pin_bit_mask = mask,
        .mode = GPIO_MODE_INPUT,
#ifdef CONFIG_BUTTON_ACTIVE_HIGH
        .pull_down_en = GPIO_PULLDOWN_ENABLE,
#else
        .pull_up_en = GPIO_PULLUP_ENABLE,
#endif
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&input));
    for (unsigned i = 0; i < CONFIG_BUTTON_COUNT; ++i) {
        button_debounce_init(&buttons[i], read_pressed(i), now_ms());
        atomic_store(&button_states[i], buttons[i].stable);
        ESP_LOGI(TAG, "Button %u: GPIO%d", i + 1, button_gpios[i]);
    }
    uint8_t mac[6];
    char mac_string[13];
    ESP_ERROR_CHECK(esp_read_mac(mac, ESP_MAC_WIFI_STA));
    snprintf(mac_string, sizeof(mac_string), "%02x%02x%02x%02x%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    ESP_ERROR_CHECK(ha_device_init(&device, mac_string) ? ESP_OK : ESP_ERR_INVALID_ARG);
    ESP_LOGI(TAG, "Device identifier: %s", device.id);
    if (!strlen(CONFIG_BUTTON_WIFI_SSID) || !strlen(CONFIG_BUTTON_MQTT_URI)) {
        ESP_LOGW(TAG, "Configure Wi-Fi SSID and MQTT URI in menuconfig > Home Assistant MQTT buttons, then rebuild/flash");
        ESP_ERROR_CHECK(xTaskCreate(button_task, "buttons", 3072, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
        return;
    }
    ESP_ERROR_CHECK(strlen(CONFIG_BUTTON_WIFI_SSID) <= 32 && strlen(CONFIG_BUTTON_WIFI_PASSWORD) <= 64
                    ? ESP_OK : ESP_ERR_INVALID_ARG);
    ESP_ERROR_CHECK(strncmp(CONFIG_BUTTON_MQTT_URI, "mqtt://", 7) == 0 || strncmp(CONFIG_BUTTON_MQTT_URI, "mqtts://", 8) == 0
                    ? ESP_OK : ESP_ERR_INVALID_ARG);
    start_network();
    ESP_ERROR_CHECK(xTaskCreate(button_task, "buttons", 3072, NULL, 4, NULL) == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}

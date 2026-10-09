#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"
#include "ha_discovery.h"

static void expect_string(cJSON *json, const char *key, const char *expected)
{
    cJSON *value = cJSON_GetObjectItemCaseSensitive(json, key);
    assert(cJSON_IsString(value));
    assert(strcmp(value->valuestring, expected) == 0);
}

int main(void)
{
    ha_device_t device, second;
    assert(ha_device_init(&device, "aabbcc112233"));
    assert(ha_device_init(&second, "aabbcc112234"));
    assert(strcmp(device.id, second.id) != 0);
    assert(!ha_device_init(&second, "bad/mac/name"));
    assert(!ha_device_init(&second, "aabbcc1122334"));
    assert(!ha_device_init(&second, NULL));
    assert(strcmp(device.availability_topic, "esp32_buttons/aabbcc112233/availability") == 0);
    char previous_id[56] = "";
    for (unsigned button = 1; button <= 4; ++button) {
        char topic[100], expected[100];
        assert(ha_config_topic(&device, button, topic, sizeof(topic)));
        snprintf(expected, sizeof(expected), "homeassistant/binary_sensor/esp32_buttons_aabbcc112233/button_%u/config", button);
        assert(strcmp(topic, expected) == 0);
        assert(ha_state_topic(&device, button, topic, sizeof(topic)));
        char *payload = ha_discovery_json(&device, button);
        assert(payload);
        cJSON *json = cJSON_Parse(payload);
        assert(json);
        free(payload);
        snprintf(expected, sizeof(expected), "Button %u", button);
        expect_string(json, "name", expected);
        expect_string(json, "state_topic", topic);
        expect_string(json, "payload_on", "ON");
        expect_string(json, "payload_off", "OFF");
        expect_string(json, "availability_topic", device.availability_topic);
        expect_string(json, "payload_available", "online");
        expect_string(json, "payload_not_available", "offline");
        assert(cJSON_GetObjectItemCaseSensitive(json, "qos")->valueint == 1);
        snprintf(expected, sizeof(expected), "esp32_buttons_aabbcc112233_button_%u", button);
        expect_string(json, "unique_id", expected);
        assert(strcmp(previous_id, expected) != 0);
        strcpy(previous_id, expected);
        cJSON *dev = cJSON_GetObjectItemCaseSensitive(json, "device");
        assert(cJSON_IsObject(dev));
        cJSON *identifiers = cJSON_GetObjectItemCaseSensitive(dev, "identifiers");
        assert(cJSON_IsArray(identifiers) && cJSON_GetArraySize(identifiers) == 1);
        assert(strcmp(cJSON_GetArrayItem(identifiers, 0)->valuestring, device.id) == 0);
        expect_string(dev, "model", "ESP32-WROOM-32D Buttons");
        assert(!cJSON_HasObjectItem(json, "command_topic"));
        assert(!strstr(cJSON_GetObjectItemCaseSensitive(json, "state_topic")->valuestring, "homeassistant/"));
        cJSON_Delete(json);
    }
    char small[2];
    assert(!ha_state_topic(&device, 1, small, sizeof(small)));
    assert(!ha_config_topic(&device, 1, small, sizeof(small)));
    assert(!ha_state_topic(&device, 0, small, sizeof(small)));
    assert(!ha_config_topic(&device, 5, small, sizeof(small)));
    assert(!ha_discovery_json(&device, 5));
    puts("PASS: four discovery entities, unique device/button IDs, state and availability payloads, bounded topics");
    return 0;
}

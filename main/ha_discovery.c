#include "ha_discovery.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

bool ha_device_init(ha_device_t *device, const char *mac)
{
    if (!device || !mac || strlen(mac) != 12) {
        return false;
    }
    for (unsigned i = 0; i < 12; ++i) {
        if (!isxdigit((unsigned char)mac[i])) {
            return false;
        }
    }
    snprintf(device->id, sizeof(device->id), "esp32_buttons_%s", mac);
    snprintf(device->base_topic, sizeof(device->base_topic), "esp32_buttons/%s", mac);
    snprintf(device->availability_topic, sizeof(device->availability_topic), "%s/availability", device->base_topic);
    return true;
}

bool ha_state_topic(const ha_device_t *device, unsigned button, char *out, size_t size)
{
    if (!device || !out || button < 1 || button > 4) {
        return false;
    }
    int length = snprintf(out, size, "%s/button_%u/state", device->base_topic, button);
    return length >= 0 && (size_t)length < size;
}

bool ha_config_topic(const ha_device_t *device, unsigned button, char *out, size_t size)
{
    if (!device || !out || button < 1 || button > 4) {
        return false;
    }
    int length = snprintf(out, size, "homeassistant/binary_sensor/%s/button_%u/config", device->id, button);
    return length >= 0 && (size_t)length < size;
}

char *ha_discovery_json(const ha_device_t *device, unsigned button)
{
    char state_topic[80];
    char unique_id[56];
    char name[16];
    if (!ha_state_topic(device, button, state_topic, sizeof(state_topic))) {
        return NULL;
    }
    snprintf(unique_id, sizeof(unique_id), "%s_button_%u", device->id, button);
    snprintf(name, sizeof(name), "Button %u", button);
    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return NULL;
    }
    bool ok = cJSON_AddStringToObject(root, "name", name) &&
              cJSON_AddStringToObject(root, "unique_id", unique_id) &&
              cJSON_AddStringToObject(root, "state_topic", state_topic) &&
              cJSON_AddStringToObject(root, "payload_on", "ON") &&
              cJSON_AddStringToObject(root, "payload_off", "OFF") &&
              cJSON_AddStringToObject(root, "availability_topic", device->availability_topic) &&
              cJSON_AddStringToObject(root, "payload_available", "online") &&
              cJSON_AddStringToObject(root, "payload_not_available", "offline") &&
              cJSON_AddNumberToObject(root, "qos", 1);
    cJSON *dev = cJSON_AddObjectToObject(root, "device");
    cJSON *identifiers = dev ? cJSON_AddArrayToObject(dev, "identifiers") : NULL;
    cJSON *identifier = identifiers ? cJSON_CreateString(device->id) : NULL;
    if (!identifier || !cJSON_AddItemToArray(identifiers, identifier)) {
        cJSON_Delete(identifier);
        ok = false;
    }
    ok = ok && dev && cJSON_AddStringToObject(dev, "name", device->id) &&
         cJSON_AddStringToObject(dev, "manufacturer", "Visitter") &&
         cJSON_AddStringToObject(dev, "model", "ESP32-WROOM-32D Buttons") &&
         cJSON_AddStringToObject(dev, "sw_version", "0.1.0");
    char *json = ok ? cJSON_PrintUnformatted(root) : NULL;
    cJSON_Delete(root);
    return json;
}

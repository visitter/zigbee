#pragma once

#include <stdbool.h>
#include <stddef.h>

typedef struct {
    char id[40];
    char base_topic[40];
    char availability_topic[64];
} ha_device_t;

/* mac must be twelve hexadecimal characters without separators. */
bool ha_device_init(ha_device_t *device, const char *mac);
bool ha_state_topic(const ha_device_t *device, unsigned button, char *out, size_t size);
bool ha_config_topic(const ha_device_t *device, unsigned button, char *out, size_t size);
/* Caller frees the returned JSON string with free(). */
char *ha_discovery_json(const ha_device_t *device, unsigned button);

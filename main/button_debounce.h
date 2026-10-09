#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool stable;
    bool candidate;
    uint32_t candidate_since_ms;
} button_debounce_t;

void button_debounce_init(button_debounce_t *button, bool pressed, uint32_t now_ms);
/* Returns true exactly once for each transition stable for debounce_ms. */
bool button_debounce_update(button_debounce_t *button, bool pressed,
                            uint32_t now_ms, uint32_t debounce_ms);

#include "button_debounce.h"

void button_debounce_init(button_debounce_t *button, bool pressed, uint32_t now_ms)
{
    button->stable = pressed;
    button->candidate = pressed;
    button->candidate_since_ms = now_ms;
}

bool button_debounce_update(button_debounce_t *button, bool pressed,
                            uint32_t now_ms, uint32_t debounce_ms)
{
    if (pressed != button->candidate) {
        button->candidate = pressed;
        button->candidate_since_ms = now_ms;
    }
    if (button->candidate != button->stable &&
        (uint32_t)(now_ms - button->candidate_since_ms) >= debounce_ms) {
        button->stable = button->candidate;
        return true;
    }
    return false;
}

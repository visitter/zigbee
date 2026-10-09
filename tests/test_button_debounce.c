#include <assert.h>
#include <stdio.h>
#include "button_debounce.h"

int main(void)
{
    button_debounce_t button;
    button_debounce_init(&button, false, 0);
    assert(!button_debounce_update(&button, true, 10, 30));
    assert(!button_debounce_update(&button, false, 15, 30));
    assert(!button_debounce_update(&button, true, 20, 30));
    assert(!button_debounce_update(&button, true, 49, 30));
    assert(button_debounce_update(&button, true, 50, 30));
    assert(button.stable);
    assert(!button_debounce_update(&button, true, 5000, 30));

    /* Release bounce must not create a false release/press pair. */
    assert(!button_debounce_update(&button, false, 5010, 30));
    assert(!button_debounce_update(&button, true, 5020, 30));
    assert(!button_debounce_update(&button, false, 5030, 30));
    assert(!button_debounce_update(&button, false, 5059, 30));
    assert(button_debounce_update(&button, false, 5060, 30));
    assert(!button.stable);

    /* A brief pulse below the threshold is rejected. */
    assert(!button_debounce_update(&button, true, 5100, 30));
    assert(!button_debounce_update(&button, false, 5120, 30));
    assert(!button_debounce_update(&button, false, 5200, 30));
    assert(!button.stable);

    /* Initial state reflects a button held at boot, without inventing an edge. */
    button_debounce_init(&button, true, 0);
    assert(button.stable);
    assert(!button_debounce_update(&button, true, 1000, 30));

    /* Monotonic millisecond counter wrap must not lose an edge. */
    button_debounce_init(&button, false, UINT32_MAX - 20);
    assert(!button_debounce_update(&button, true, UINT32_MAX - 10, 30));
    assert(!button_debounce_update(&button, true, 18, 30));
    assert(button_debounce_update(&button, true, 19, 30));

    /* Separate buttons keep independent debounce histories. */
    button_debounce_t second;
    button_debounce_init(&second, false, 0);
    assert(!button_debounce_update(&second, true, 100, 30));
    assert(!button_debounce_update(&button, false, 110, 30));
    assert(button_debounce_update(&second, true, 130, 30));
    assert(button.stable);
    assert(button_debounce_update(&button, false, 140, 30));
    assert(second.stable);
    puts("PASS: press/release bounce, held state, short pulse, boot state, timer wrap, independent buttons");
    return 0;
}

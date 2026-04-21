#include "relay.h"
#include "driver/gpio.h"

static bool s_relay_state = false;

void relay_init(void) {
    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << RELAY_GPIO),
        .mode         = GPIO_MODE_OUTPUT,
        .pull_up_en   = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type    = GPIO_INTR_DISABLE,
    };
    gpio_config(&cfg);
    relay_set(false);
}

void relay_set(bool on) {
    s_relay_state = on;
    gpio_set_level(RELAY_GPIO, on ? 1 : 0);
}

bool relay_get(void) {
    return s_relay_state;
}

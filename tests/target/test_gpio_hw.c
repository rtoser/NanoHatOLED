#include <stdio.h>

#include "hal/gpio_hal.h"

static const char *event_name(gpio_event_type_t type) {
    switch (type) {
        case GPIO_EVT_BTN_K1_SHORT: return "K1_SHORT";
        case GPIO_EVT_BTN_K2_SHORT: return "K2_SHORT";
        case GPIO_EVT_BTN_K3_SHORT: return "K3_SHORT";
        case GPIO_EVT_BTN_K1_LONG: return "K1_LONG";
        case GPIO_EVT_BTN_K2_LONG: return "K2_LONG";
        case GPIO_EVT_BTN_K3_LONG: return "K3_LONG";
        default: return "UNKNOWN";
    }
}

int main(void) {
    printf("=== GPIO Hardware Test ===\n");
    if (gpio_hal->init() != 0) {
        printf("FAIL: gpio init\n");
        return 1;
    }

    printf("Press any key within 10 seconds...\n");
    gpio_event_t evt;
    int ret = gpio_hal->wait_event(10000, &evt);
    if (ret > 0) {
        printf("PASS: event=%s line=%u ts=%llu\n",
               event_name(evt.type),
               (unsigned int)evt.line,
               (unsigned long long)evt.timestamp_ns);
        gpio_hal->cleanup();
        return 0;
    }

    printf("FAIL: ret=%d\n", ret);
    gpio_hal->cleanup();
    return 1;
}

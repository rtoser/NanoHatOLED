#include <stdio.h>

#include "hal/gpio_hal.h"

int main(void) {
    printf("=== GPIO Hardware Test ===\n");
    if (gpio_hal->init() != 0) {
        printf("FAIL: gpio init\n");
        return 1;
    }

    printf("Press K1 within 5 seconds...\n");
    gpio_event_t evt;
    int ret = gpio_hal->wait_event(5000, &evt);
    if (ret > 0 && evt.type == GPIO_EVT_BTN_K1_SHORT) {
        printf("PASS: K1 short press detected\n");
    } else {
        printf("FAIL: ret=%d type=%d\n", ret, evt.type);
        gpio_hal->cleanup();
        return 1;
    }

    gpio_hal->cleanup();
    return 0;
}

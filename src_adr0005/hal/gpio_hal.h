#ifndef GPIO_HAL_H
#define GPIO_HAL_H

#include <stdint.h>

typedef enum {
    GPIO_EVT_NONE = 0,
    GPIO_EVT_BTN_K1_SHORT,
    GPIO_EVT_BTN_K2_SHORT,
    GPIO_EVT_BTN_K3_SHORT,
    GPIO_EVT_BTN_K1_LONG,
    GPIO_EVT_BTN_K2_LONG,
    GPIO_EVT_BTN_K3_LONG
} gpio_event_type_t;

typedef struct {
    gpio_event_type_t type;
    uint8_t line;
    uint64_t timestamp_ns;
} gpio_event_t;

typedef struct {
    int  (*init)(void);
    void (*cleanup)(void);
    int  (*wait_event)(int timeout_ms, gpio_event_t *event);
    int  (*get_fd)(void);
} gpio_hal_ops_t;

extern const gpio_hal_ops_t *gpio_hal;

#endif

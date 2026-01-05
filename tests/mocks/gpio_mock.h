#ifndef GPIO_MOCK_H
#define GPIO_MOCK_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    EDGE_RISING = 0,
    EDGE_FALLING = 1
} edge_type_t;

void gpio_mock_inject_edge(int line, edge_type_t type, uint64_t timestamp_ns);
void gpio_mock_set_line_value(int line, int value);
void gpio_mock_clear_events(void);
int  gpio_mock_get_pending_count(void);
int  gpio_mock_get_fd(void);
void gpio_mock_set_debounce_supported(bool supported);

#endif

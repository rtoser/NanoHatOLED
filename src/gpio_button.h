#ifndef GPIO_BUTTON_H
#define GPIO_BUTTON_H

#include <stdint.h>
#include <stdbool.h>

// NanoHat OLED button line offsets on gpiochip1 (Allwinner H5)
#define BTN_K1_GPIO  0   // PA0 - typically "up" or "select"
#define BTN_K2_GPIO  2   // PA2 - typically "down" or "back"
#define BTN_K3_GPIO  3   // PA3 - typically "enter" or "menu"

// Button events
typedef enum {
    BTN_NONE = 0,
    BTN_K1_PRESS,        // Short press (on release)
    BTN_K2_PRESS,        // Short press (on release)
    BTN_K3_PRESS,        // Short press (on release)
    BTN_K1_LONG_PRESS,
    BTN_K2_LONG_PRESS,
    BTN_K3_LONG_PRESS
} button_event_t;

// Initialize GPIO buttons (uses libgpiod v2)
int gpio_button_init(void);

// Cleanup GPIO buttons
void gpio_button_cleanup(void);

// Poll button state, returns button event (non-blocking)
button_event_t gpio_button_poll(void);

// Wait for button event with interrupt, timeout in ms
button_event_t gpio_button_wait(int timeout_ms);

#endif

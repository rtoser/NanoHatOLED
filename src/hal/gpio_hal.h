/*
 * GPIO HAL for NanoHat OLED
 *
 * Abstracts button input. Designed for uloop integration:
 * - get_fd() returns pollable fd for uloop_fd
 * - read_event() is non-blocking, called when fd is readable
 *
 * Implementations handle debounce and long-press detection internally.
 */
#ifndef GPIO_HAL_H
#define GPIO_HAL_H

#include <stdint.h>

/*
 * Button event types.
 * Short press: released before LONG_PRESS_MS (600ms)
 * Long press: emitted once when held for >= LONG_PRESS_MS (no release required)
 */
typedef enum {
    GPIO_EVT_NONE = 0,
    GPIO_EVT_BTN_K1_SHORT,
    GPIO_EVT_BTN_K2_SHORT,
    GPIO_EVT_BTN_K3_SHORT,
    GPIO_EVT_BTN_K1_LONG,
    GPIO_EVT_BTN_K2_LONG,
    GPIO_EVT_BTN_K3_LONG
} gpio_event_type_t;

/*
 * Button event structure.
 */
typedef struct {
    gpio_event_type_t type;
    uint8_t line;           /* Button index: 0=K1, 1=K2, 2=K3 */
    uint64_t timestamp_ns;  /* Event timestamp (CLOCK_MONOTONIC) */
} gpio_event_t;

/*
 * GPIO HAL operations.
 */
typedef struct {
    /*
     * Initialize GPIO hardware.
     * Returns: 0 on success, -1 on failure
     */
    int (*init)(void);

    /*
     * Cleanup GPIO hardware.
     */
    void (*cleanup)(void);

    /*
     * Get file descriptor for event polling.
     * Used with uloop_fd to monitor button events.
     * Returns: fd >= 0 on success, -1 if not available
     */
    int (*get_fd)(void);

    /*
     * Optional timer fd for long-press threshold events.
     * When available, register with uloop_fd and call read_event() on readiness.
     * Returns: fd >= 0 on success, -1 if not available
     */
    int (*get_timer_fd)(void);

    /*
     * Read next button event (non-blocking).
     * Call this when get_fd() is readable.
     *
     * Returns:
     *   1  - Event available, stored in *event
     *   0  - No complete event yet (e.g., button still pressed)
     *   -1 - Error
     */
    int (*read_event)(gpio_event_t *event);
} gpio_hal_ops_t;

/*
 * Global GPIO HAL instance.
 * Set by linking appropriate implementation.
 */
extern const gpio_hal_ops_t *gpio_hal;

/*
 * Configuration constants.
 */
#define GPIO_NUM_BUTTONS    3
#define GPIO_LONG_PRESS_MS  600
#define GPIO_DEBOUNCE_MS    30

#endif

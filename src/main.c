/*
 * NanoHat OLED - ADR0006 Single-threaded uloop implementation
 *
 * Main entry point with libubox/uloop event loop.
 */
#include <stdio.h>
#include <signal.h>
#include <libubox/uloop.h>

#include "hal/display_hal.h"
#include "hal/gpio_hal.h"
#include "hal/time_hal.h"
#include "ui_controller.h"

#define APP_NAME "nanohat-oled"

/* Forward declarations */
static void gpio_fd_cb(struct uloop_fd *u, unsigned int events);
static void handle_button_event(const gpio_event_t *event);
static void ui_timer_cb(struct uloop_timeout *t);
static void schedule_ui_timer(void);

/*
 * Signal handlers - static to ensure lifetime
 */
static struct uloop_signal sig_term;
static struct uloop_signal sig_int;

/*
 * GPIO fd for uloop integration
 */
static struct uloop_fd gpio_uloop_fd;
static struct uloop_fd gpio_timer_uloop_fd;

/*
 * UI controller and refresh timer
 */
static ui_controller_t g_ui;
static struct uloop_timeout g_ui_timer;

/*
 * Signal callback - called by uloop when signal received.
 * This is async-signal-safe because uloop handles the signal internally
 * via signalfd/self-pipe and invokes this callback from the main loop context.
 */
static void handle_signal(struct uloop_signal *s) {
    printf("%s received signal %d, shutting down...\n", APP_NAME, s->signo);
    uloop_end();
}

/*
 * Button event handler - called when a button press/release is detected
 */
static void handle_button_event(const gpio_event_t *event) {
    if (!event) {
        return;
    }

    uint64_t now_ms = event->timestamp_ns / 1000000ULL;

    uint8_t key = 0;
    bool long_press = false;
    switch (event->type) {
        case GPIO_EVT_BTN_K1_SHORT: key = KEY_K1; break;
        case GPIO_EVT_BTN_K2_SHORT: key = KEY_K2; break;
        case GPIO_EVT_BTN_K3_SHORT: key = KEY_K3; break;
        case GPIO_EVT_BTN_K1_LONG:  key = KEY_K1; long_press = true; break;
        case GPIO_EVT_BTN_K2_LONG:  key = KEY_K2; long_press = true; break;
        case GPIO_EVT_BTN_K3_LONG:  key = KEY_K3; long_press = true; break;
        default: break;
    }

    if (key != 0) {
        ui_controller_handle_button(&g_ui, key, long_press, now_ms);
        ui_controller_render(&g_ui, now_ms);
        schedule_ui_timer();
    }
}

/*
 * GPIO fd callback - called by uloop when GPIO fd is readable
 */
static void gpio_fd_cb(struct uloop_fd *u, unsigned int events) {
    (void)u;
    (void)events;

    gpio_event_t event;
    int ret;

    /* Read all available events */
    while ((ret = gpio_hal->read_event(&event)) > 0) {
        handle_button_event(&event);
    }

    if (ret < 0) {
        fprintf(stderr, "WARN: gpio read_event error\n");
    }
}

static void ui_timer_cb(struct uloop_timeout *t) {
    (void)t;

    uint64_t now_ms = time_hal_now_ms();
    ui_controller_tick(&g_ui, now_ms);
    ui_controller_render(&g_ui, now_ms);
    schedule_ui_timer();
}

static void schedule_ui_timer(void) {
    int next_ms = ui_controller_next_timeout_ms(&g_ui);
    if (next_ms > 0) {
        g_ui_timer.cb = ui_timer_cb;
        uloop_timeout_set(&g_ui_timer, next_ms);
    }
}

/*
 * Cleanup HAL resources
 */
static void cleanup_hal(void) {
    if (gpio_hal && gpio_hal->cleanup) {
        gpio_hal->cleanup();
    }
    if (display_hal && display_hal->cleanup) {
        display_hal->cleanup();
    }
}

int main(void) {
    int rc = 0;

    printf("%s starting (ADR0006 uloop)...\n", APP_NAME);

    /* 1. Initialize HAL */
    if (display_hal->init() != 0) {
        fprintf(stderr, "FAIL: display_hal init\n");
        return 1;
    }

    if (gpio_hal->init() != 0) {
        fprintf(stderr, "FAIL: gpio_hal init\n");
        cleanup_hal();
        return 1;
    }

    ui_controller_init(&g_ui);

    /* 2. Initialize uloop */
    if (uloop_init() != 0) {
        fprintf(stderr, "FAIL: uloop_init\n");
        cleanup_hal();
        return 1;
    }

    /* 3. Register signal handlers via uloop (async-signal-safe) */
    sig_term.cb = handle_signal;
    sig_term.signo = SIGTERM;
    if (uloop_signal_add(&sig_term) < 0) {
        fprintf(stderr, "WARN: failed to register SIGTERM handler\n");
    }

    sig_int.cb = handle_signal;
    sig_int.signo = SIGINT;
    if (uloop_signal_add(&sig_int) < 0) {
        fprintf(stderr, "WARN: failed to register SIGINT handler\n");
    }

    /* 4. Register GPIO fd with uloop */
    int gpio_fd = gpio_hal->get_fd();
    if (gpio_fd >= 0) {
        gpio_uloop_fd.fd = gpio_fd;
        gpio_uloop_fd.cb = gpio_fd_cb;
        if (uloop_fd_add(&gpio_uloop_fd, ULOOP_READ) < 0) {
            fprintf(stderr, "WARN: failed to add GPIO fd to uloop\n");
        }
    } else {
        fprintf(stderr, "WARN: GPIO fd not available\n");
    }

    /* Optional timer fd for long-press threshold */
    if (gpio_hal->get_timer_fd) {
        int timer_fd = gpio_hal->get_timer_fd();
        if (timer_fd >= 0) {
            gpio_timer_uloop_fd.fd = timer_fd;
            gpio_timer_uloop_fd.cb = gpio_fd_cb;
            if (uloop_fd_add(&gpio_timer_uloop_fd, ULOOP_READ) < 0) {
                fprintf(stderr, "WARN: failed to add GPIO timer fd to uloop\n");
            }
        }
    }

    /* Initial render and timer schedule */
    uint64_t now_ms = time_hal_now_ms();
    ui_controller_tick(&g_ui, now_ms);
    ui_controller_render(&g_ui, now_ms);
    schedule_ui_timer();

    /* 5. Run main event loop */
    printf("%s started\n", APP_NAME);
    uloop_run();

    /* 6. Cleanup */
    printf("%s shutting down...\n", APP_NAME);
    uloop_done();
    ui_controller_cleanup(&g_ui);
    cleanup_hal();

    printf("%s exit\n", APP_NAME);
    return rc;
}

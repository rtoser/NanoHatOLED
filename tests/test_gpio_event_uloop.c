/*
 * GPIO event test with uloop integration
 *
 * Tests the gpio_hal_mock implementation with uloop event loop.
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libubox/uloop.h>

#include "gpio_hal.h"
#include "time_hal.h"

/* Test state */
static int events_received = 0;
static gpio_event_type_t last_event_type = GPIO_EVT_NONE;
static int last_event_line = -1;

/* GPIO fd for uloop */
static struct uloop_fd gpio_uloop_fd;
static struct uloop_fd gpio_timer_uloop_fd;

/* Timeout to end test */
static struct uloop_timeout test_timeout;
static struct uloop_timeout release_timeout;
static int release_line = -1;

/* Allow enough time for long-press threshold */
#define LONG_WAIT_MS (GPIO_LONG_PRESS_MS + 200)

/* Forward declarations from gpio_hal_mock.c */
extern void gpio_mock_inject_edge(int line, int falling, uint64_t timestamp_ns);
extern void gpio_mock_inject_press(int line, bool long_press);
extern void gpio_mock_clear(void);

static void release_cb(struct uloop_timeout *t) {
    (void)t;
    if (release_line >= 0) {
        gpio_mock_inject_edge(release_line, 0, time_hal_now_ns());
    }
}

static void handle_gpio_event(const gpio_event_t *event) {
    events_received++;
    last_event_type = event->type;
    last_event_line = event->line;
    printf("  Event: line=%d type=%d\n", event->line, event->type);
}

static void gpio_fd_cb(struct uloop_fd *u, unsigned int events) {
    (void)u;
    (void)events;

    gpio_event_t event;
    while (gpio_hal->read_event(&event) > 0) {
        handle_gpio_event(&event);
    }
}

static void setup_gpio_fds(void) {
    memset(&gpio_uloop_fd, 0, sizeof(gpio_uloop_fd));
    gpio_uloop_fd.fd = gpio_hal->get_fd();
    gpio_uloop_fd.cb = gpio_fd_cb;
    uloop_fd_add(&gpio_uloop_fd, ULOOP_READ);

    if (gpio_hal->get_timer_fd) {
        int timer_fd = gpio_hal->get_timer_fd();
        if (timer_fd >= 0) {
            memset(&gpio_timer_uloop_fd, 0, sizeof(gpio_timer_uloop_fd));
            gpio_timer_uloop_fd.fd = timer_fd;
            gpio_timer_uloop_fd.cb = gpio_fd_cb;
            uloop_fd_add(&gpio_timer_uloop_fd, ULOOP_READ);
        }
    }
}

static void timeout_cb(struct uloop_timeout *t) {
    (void)t;
    printf("  Timeout reached\n");
    uloop_end();
}

static int test_gpio_init(void) {
    printf("Test: GPIO init\n");

    if (gpio_hal->init() != 0) {
        printf("  FAIL: gpio_hal->init()\n");
        return 1;
    }

    int fd = gpio_hal->get_fd();
    if (fd < 0) {
        printf("  FAIL: gpio_hal->get_fd() returned %d\n", fd);
        gpio_hal->cleanup();
        return 1;
    }

    int timer_fd = -1;
    if (gpio_hal->get_timer_fd) {
        timer_fd = gpio_hal->get_timer_fd();
    }
    printf("  PASS: GPIO init, fd=%d timer_fd=%d\n", fd, timer_fd);
    return 0;
}

static int test_gpio_short_press(void) {
    printf("Test: GPIO short press\n");

    events_received = 0;
    last_event_type = GPIO_EVT_NONE;
    last_event_line = -1;
    gpio_mock_clear();

    /* Inject short press on K1 (line 0) */
    gpio_mock_inject_press(0, false);

    /* Give uloop a chance to process */
    uloop_init();

    setup_gpio_fds();

    /* Short timeout to allow event processing */
    test_timeout.cb = timeout_cb;
    uloop_timeout_set(&test_timeout, 100);

    uloop_run();
    uloop_done();

    if (events_received != 1) {
        printf("  FAIL: expected 1 event, got %d\n", events_received);
        return 1;
    }

    if (last_event_type != GPIO_EVT_BTN_K1_SHORT) {
        printf("  FAIL: expected K1_SHORT (%d), got %d\n",
               GPIO_EVT_BTN_K1_SHORT, last_event_type);
        return 1;
    }

    if (last_event_line != 0) {
        printf("  FAIL: expected line 0, got %d\n", last_event_line);
        return 1;
    }

    printf("  PASS: short press event received\n");
    return 0;
}

static int test_gpio_long_press(void) {
    printf("Test: GPIO long press\n");

    events_received = 0;
    last_event_type = GPIO_EVT_NONE;
    last_event_line = -1;
    gpio_mock_clear();

    /* Inject long press on K2 (line 1) */
    gpio_mock_inject_press(1, true);

    /* Give uloop a chance to process */
    uloop_init();

    setup_gpio_fds();

    /* Wait long enough for long-press threshold */
    test_timeout.cb = timeout_cb;
    uloop_timeout_set(&test_timeout, LONG_WAIT_MS);

    uloop_run();
    uloop_done();

    if (events_received != 1) {
        printf("  FAIL: expected 1 event, got %d\n", events_received);
        return 1;
    }

    if (last_event_type != GPIO_EVT_BTN_K2_LONG) {
        printf("  FAIL: expected K2_LONG (%d), got %d\n",
               GPIO_EVT_BTN_K2_LONG, last_event_type);
        return 1;
    }

    if (last_event_line != 1) {
        printf("  FAIL: expected line 1, got %d\n", last_event_line);
        return 1;
    }

    printf("  PASS: long press event received\n");
    return 0;
}

static int test_gpio_long_press_no_release_event(void) {
    printf("Test: GPIO long press no release event\n");

    events_received = 0;
    last_event_type = GPIO_EVT_NONE;
    last_event_line = -1;
    gpio_mock_clear();

    /* Inject press only */
    gpio_mock_inject_edge(1, 1, time_hal_now_ns());

    uloop_init();

    setup_gpio_fds();

    /* Release after long press should already be emitted */
    release_line = 1;
    release_timeout.cb = release_cb;
    uloop_timeout_set(&release_timeout, LONG_WAIT_MS + 50);

    test_timeout.cb = timeout_cb;
    uloop_timeout_set(&test_timeout, LONG_WAIT_MS + 200);

    uloop_run();
    uloop_done();

    if (events_received != 1) {
        printf("  FAIL: expected 1 event, got %d\n", events_received);
        return 1;
    }

    if (last_event_type != GPIO_EVT_BTN_K2_LONG) {
        printf("  FAIL: expected K2_LONG (%d), got %d\n",
               GPIO_EVT_BTN_K2_LONG, last_event_type);
        return 1;
    }

    printf("  PASS: no short event after release\n");
    return 0;
}

static int test_gpio_release_before_threshold(void) {
    printf("Test: GPIO release before threshold\n");

    events_received = 0;
    last_event_type = GPIO_EVT_NONE;
    last_event_line = -1;
    gpio_mock_clear();

    /* Press K1, release before long-press threshold */
    gpio_mock_inject_edge(0, 1, time_hal_now_ns());

    uloop_init();

    setup_gpio_fds();

    release_line = 0;
    release_timeout.cb = release_cb;
    uloop_timeout_set(&release_timeout, GPIO_LONG_PRESS_MS - 100);

    test_timeout.cb = timeout_cb;
    uloop_timeout_set(&test_timeout, LONG_WAIT_MS + 200);

    uloop_run();
    uloop_done();

    if (events_received != 1) {
        printf("  FAIL: expected 1 event, got %d\n", events_received);
        return 1;
    }

    if (last_event_type != GPIO_EVT_BTN_K1_SHORT) {
        printf("  FAIL: expected K1_SHORT (%d), got %d\n",
               GPIO_EVT_BTN_K1_SHORT, last_event_type);
        return 1;
    }

    printf("  PASS: short press before threshold\n");
    return 0;
}

static int test_gpio_debounce(void) {
    printf("Test: GPIO debounce\n");

    events_received = 0;
    last_event_type = GPIO_EVT_NONE;
    last_event_line = -1;
    gpio_mock_clear();

    uint64_t now_ns = time_hal_now_ns();
    gpio_mock_inject_edge(0, 1, now_ns);
    gpio_mock_inject_edge(0, 0, now_ns + 5 * 1000000ULL);
    gpio_mock_inject_edge(0, 1, now_ns + 10 * 1000000ULL);
    gpio_mock_inject_edge(0, 0, now_ns + 40 * 1000000ULL);

    uloop_init();

    setup_gpio_fds();

    test_timeout.cb = timeout_cb;
    uloop_timeout_set(&test_timeout, 100);

    uloop_run();
    uloop_done();

    if (events_received != 1) {
        printf("  FAIL: expected 1 event, got %d\n", events_received);
        return 1;
    }

    if (last_event_type != GPIO_EVT_BTN_K1_SHORT) {
        printf("  FAIL: expected K1_SHORT (%d), got %d\n",
               GPIO_EVT_BTN_K1_SHORT, last_event_type);
        return 1;
    }

    printf("  PASS: debounce filtered bounce edges\n");
    return 0;
}

static int test_gpio_multiple_buttons(void) {
    printf("Test: GPIO multiple buttons\n");

    events_received = 0;
    gpio_mock_clear();

    /* Inject presses on all three buttons */
    gpio_mock_inject_press(0, false);  /* K1 short */
    gpio_mock_inject_press(1, false);  /* K2 short */
    gpio_mock_inject_press(2, true);   /* K3 long */

    /* Give uloop a chance to process */
    uloop_init();

    setup_gpio_fds();

    /* Wait long enough for long-press threshold */
    test_timeout.cb = timeout_cb;
    uloop_timeout_set(&test_timeout, LONG_WAIT_MS);

    uloop_run();
    uloop_done();

    if (events_received != 3) {
        printf("  FAIL: expected 3 events, got %d\n", events_received);
        return 1;
    }

    printf("  PASS: all button events received\n");
    return 0;
}

int main(void) {
    int failures = 0;

    printf("=== GPIO Event + uloop Tests ===\n");

    failures += test_gpio_init();
    failures += test_gpio_short_press();
    failures += test_gpio_long_press();
    failures += test_gpio_long_press_no_release_event();
    failures += test_gpio_release_before_threshold();
    failures += test_gpio_debounce();
    failures += test_gpio_multiple_buttons();

    gpio_hal->cleanup();

    printf("\n=== Results: %d failures ===\n", failures);
    return failures > 0 ? 1 : 0;
}

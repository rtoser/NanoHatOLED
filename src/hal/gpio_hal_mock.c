/*
 * GPIO HAL mock implementation for testing
 *
 * Uses eventfd (Linux) or pipe (other platforms) to simulate button events.
 * Test code can inject events via gpio_mock_inject_press().
 */
#include "gpio_hal.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__)
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#define USE_EVENTFD 1
#define USE_TIMERFD 1
#else
#define USE_EVENTFD 0
#define USE_TIMERFD 0
#endif

#include "time_hal.h"

#define MAX_PENDING_EVENTS 32

/* Edge type for internal use */
typedef enum {
    EDGE_RISING = 0,
    EDGE_FALLING = 1
} edge_type_t;

typedef struct {
    int line;
    edge_type_t type;
    uint64_t timestamp_ns;
} mock_edge_t;

#define MAX_EDGE_EVENTS 64

/* Static state */
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_notify_fd = -1;
static int g_notify_write_fd = -1;
static int g_timer_fd = -1;

static mock_edge_t g_edges[MAX_EDGE_EVENTS];
static size_t g_edge_head = 0;
static size_t g_edge_tail = 0;
static size_t g_edge_count = 0;

static gpio_event_t g_pending[MAX_PENDING_EVENTS];
static size_t g_pending_head = 0;
static size_t g_pending_tail = 0;
static size_t g_pending_count = 0;

static uint64_t g_press_time_ms[GPIO_NUM_BUTTONS];
static uint64_t g_last_edge_ms[GPIO_NUM_BUTTONS];
static bool g_pressed[GPIO_NUM_BUTTONS];
static bool g_long_sent[GPIO_NUM_BUTTONS];

static int g_line_values[GPIO_NUM_BUTTONS] = {1, 1, 1};
static int g_pressed_level = 0;
static bool g_use_soft_debounce = true;

/* Queue operations */
static void reset_state(void) {
    memset(g_press_time_ms, 0, sizeof(g_press_time_ms));
    memset(g_last_edge_ms, 0, sizeof(g_last_edge_ms));
    memset(g_pressed, 0, sizeof(g_pressed));
    memset(g_long_sent, 0, sizeof(g_long_sent));
    g_edge_head = g_edge_tail = g_edge_count = 0;
    g_pending_head = g_pending_tail = g_pending_count = 0;
}

static void signal_fd(void) {
    if (g_notify_write_fd >= 0) {
#if USE_EVENTFD
        uint64_t v = 1;
        (void)write(g_notify_write_fd, &v, sizeof(v));
#else
        uint8_t v = 1;
        (void)write(g_notify_write_fd, &v, sizeof(v));
#endif
    }
}

static void drain_fd(void) {
    if (g_notify_fd < 0) return;
#if USE_EVENTFD
    uint64_t v;
    while (read(g_notify_fd, &v, sizeof(v)) > 0) {}
#else
    uint8_t v;
    while (read(g_notify_fd, &v, sizeof(v)) > 0) {}
#endif
}

static void push_edge(const mock_edge_t *edge) {
    if (g_edge_count >= MAX_EDGE_EVENTS) {
        g_edge_head = (g_edge_head + 1) % MAX_EDGE_EVENTS;
        g_edge_count--;
    }
    g_edges[g_edge_tail] = *edge;
    g_edge_tail = (g_edge_tail + 1) % MAX_EDGE_EVENTS;
    g_edge_count++;
}

static bool pop_edge(mock_edge_t *edge) {
    if (g_edge_count == 0) return false;
    *edge = g_edges[g_edge_head];
    g_edge_head = (g_edge_head + 1) % MAX_EDGE_EVENTS;
    g_edge_count--;
    return true;
}

static void push_pending(const gpio_event_t *event) {
    if (g_pending_count >= MAX_PENDING_EVENTS) {
        g_pending_head = (g_pending_head + 1) % MAX_PENDING_EVENTS;
        g_pending_count--;
    }
    g_pending[g_pending_tail] = *event;
    g_pending_tail = (g_pending_tail + 1) % MAX_PENDING_EVENTS;
    g_pending_count++;
}

static bool pop_pending(gpio_event_t *event) {
    if (g_pending_count == 0) return false;
    *event = g_pending[g_pending_head];
    g_pending_head = (g_pending_head + 1) % MAX_PENDING_EVENTS;
    g_pending_count--;
    return true;
}

static gpio_event_type_t to_button_event(int line, bool long_press) {
    switch (line) {
    case 0: return long_press ? GPIO_EVT_BTN_K1_LONG : GPIO_EVT_BTN_K1_SHORT;
    case 1: return long_press ? GPIO_EVT_BTN_K2_LONG : GPIO_EVT_BTN_K2_SHORT;
    default: return long_press ? GPIO_EVT_BTN_K3_LONG : GPIO_EVT_BTN_K3_SHORT;
    }
}

static int detect_pressed_level(void) {
    int zeros = 0, ones = 0;
    for (int i = 0; i < GPIO_NUM_BUTTONS; i++) {
        if (g_line_values[i] == 0) zeros++; else ones++;
    }
    return (zeros >= ones) ? 1 : 0;
}

#if USE_TIMERFD
static void update_long_press_timer_locked(void) {
    if (g_timer_fd < 0) return;

    uint64_t now_ms = time_hal_now_ms();
    uint64_t next_delay_ms = 0;
    bool has_pending = false;
    for (int i = 0; i < GPIO_NUM_BUTTONS; i++) {
        if (g_pressed[i] && !g_long_sent[i]) {
            has_pending = true;
            uint64_t deadline = g_press_time_ms[i] + GPIO_LONG_PRESS_MS;
            uint64_t remaining = (deadline > now_ms) ? (deadline - now_ms) : 0;
            if (next_delay_ms == 0 || remaining < next_delay_ms) {
                next_delay_ms = remaining;
            }
        }
    }

    struct itimerspec its;
    memset(&its, 0, sizeof(its));
    if (has_pending) {
        if (next_delay_ms == 0) {
            next_delay_ms = 1;
        }
        its.it_value.tv_sec = (time_t)(next_delay_ms / 1000);
        its.it_value.tv_nsec = (long)((next_delay_ms % 1000) * 1000000L);
        timerfd_settime(g_timer_fd, 0, &its, NULL);
    } else {
        timerfd_settime(g_timer_fd, 0, &its, NULL);
    }
}

static void handle_long_press_timer_locked(void) {
    if (g_timer_fd < 0) return;

    uint64_t now_ms = time_hal_now_ms();
    uint64_t now_ns = time_hal_now_ns();

    for (int i = 0; i < GPIO_NUM_BUTTONS; i++) {
        if (g_pressed[i] && !g_long_sent[i] &&
            now_ms - g_press_time_ms[i] >= GPIO_LONG_PRESS_MS) {
            g_long_sent[i] = true;
            gpio_event_t evt = {
                .type = to_button_event(i, true),
                .line = (uint8_t)i,
                .timestamp_ns = now_ns
            };
            push_pending(&evt);
        }
    }

    update_long_press_timer_locked();
}
#endif

static void process_edge(const mock_edge_t *edge) {
    uint64_t now_ms = edge->timestamp_ns / 1000000ULL;
    int line = edge->line;
    if (line < 0 || line >= GPIO_NUM_BUTTONS) return;

    /* Debounce */
    if (g_use_soft_debounce && g_last_edge_ms[line] != 0 &&
        now_ms - g_last_edge_ms[line] < GPIO_DEBOUNCE_MS) {
        return;
    }
    g_last_edge_ms[line] = now_ms;

    int new_value = (edge->type == EDGE_FALLING) ? 0 : 1;
    g_line_values[line] = new_value;
    bool is_pressed = (new_value == g_pressed_level);

    bool use_longpress_timer = (g_timer_fd >= 0);

    if (is_pressed) {
        g_pressed[line] = true;
        g_press_time_ms[line] = now_ms;
        g_long_sent[line] = false;
#if USE_TIMERFD
        if (use_longpress_timer) {
            update_long_press_timer_locked();
        }
#endif
        return;
    }

    if (g_pressed[line]) {
        g_pressed[line] = false;
        if (!use_longpress_timer) {
            uint64_t elapsed = now_ms - g_press_time_ms[line];
            gpio_event_t evt = {
                .type = to_button_event(line, elapsed >= GPIO_LONG_PRESS_MS),
                .line = (uint8_t)line,
                .timestamp_ns = edge->timestamp_ns
            };
            push_pending(&evt);
        } else {
            bool long_sent = g_long_sent[line];
            g_long_sent[line] = false;
            if (!long_sent) {
                gpio_event_t evt = {
                    .type = to_button_event(line, false),
                    .line = (uint8_t)line,
                    .timestamp_ns = edge->timestamp_ns
                };
                push_pending(&evt);
            }
#if USE_TIMERFD
            update_long_press_timer_locked();
#endif
        }
    }
}

/* HAL Interface */

static int mock_init(void) {
    pthread_mutex_lock(&g_lock);
    reset_state();
    g_pressed_level = detect_pressed_level();

    /* Close existing fds (save values first to avoid double-close) */
    int old_read_fd = g_notify_fd;
    int old_write_fd = g_notify_write_fd;
    g_notify_fd = -1;
    g_notify_write_fd = -1;

    if (old_read_fd >= 0) {
        close(old_read_fd);
    }
    if (old_write_fd >= 0 && old_write_fd != old_read_fd) {
        close(old_write_fd);
    }
#if USE_TIMERFD
    if (g_timer_fd >= 0) {
        close(g_timer_fd);
        g_timer_fd = -1;
    }
#endif

    /* Create notification fd */
#if USE_EVENTFD
    g_notify_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    g_notify_write_fd = g_notify_fd;
#else
    int fds[2];
    if (pipe(fds) == 0) {
        g_notify_fd = fds[0];
        g_notify_write_fd = fds[1];
        fcntl(g_notify_fd, F_SETFL, O_NONBLOCK);
        fcntl(g_notify_write_fd, F_SETFL, O_NONBLOCK);
    }
#endif

#if USE_TIMERFD
    g_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (g_timer_fd < 0) {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }
#endif

    pthread_mutex_unlock(&g_lock);
    return (g_notify_fd >= 0) ? 0 : -1;
}

static void mock_cleanup(void) {
    pthread_mutex_lock(&g_lock);

    /* Save values first to avoid double-close */
    int old_read_fd = g_notify_fd;
    int old_write_fd = g_notify_write_fd;
    g_notify_fd = -1;
    g_notify_write_fd = -1;

    if (old_read_fd >= 0) {
        close(old_read_fd);
    }
    if (old_write_fd >= 0 && old_write_fd != old_read_fd) {
        close(old_write_fd);
    }
#if USE_TIMERFD
    if (g_timer_fd >= 0) {
        close(g_timer_fd);
        g_timer_fd = -1;
    }
#endif

    reset_state();
    pthread_mutex_unlock(&g_lock);
}

static int mock_get_fd(void) {
    return g_notify_fd;
}

static int mock_get_timer_fd(void) {
#if USE_TIMERFD
    return g_timer_fd;
#else
    return -1;
#endif
}

static int mock_read_event(gpio_event_t *event) {
    if (!event) return -1;

    pthread_mutex_lock(&g_lock);

    /* Drain timerfd if needed and emit long-press events */
#if USE_TIMERFD
    if (g_timer_fd >= 0) {
        uint64_t expirations;
        while (read(g_timer_fd, &expirations, sizeof(expirations)) > 0) {
            /* drain */
        }
        handle_long_press_timer_locked();
    }
#endif

    if (pop_pending(event)) {
        pthread_mutex_unlock(&g_lock);
        return 1;
    }

    drain_fd();
    mock_edge_t edge;
    while (pop_edge(&edge)) {
        process_edge(&edge);
    }

    /* Return next event */
    bool has_event = pop_pending(event);
    pthread_mutex_unlock(&g_lock);
    return has_event ? 1 : 0;
}

/* HAL operations table */
static const gpio_hal_ops_t mock_ops = {
    .init = mock_init,
    .cleanup = mock_cleanup,
    .get_fd = mock_get_fd,
    .get_timer_fd = mock_get_timer_fd,
    .read_event = mock_read_event,
};

const gpio_hal_ops_t *gpio_hal = &mock_ops;

/* Test injection API */

void gpio_mock_inject_edge(int line, int falling, uint64_t timestamp_ns) {
    mock_edge_t edge = {
        .line = line,
        .type = falling ? EDGE_FALLING : EDGE_RISING,
        .timestamp_ns = timestamp_ns
    };
    pthread_mutex_lock(&g_lock);
    push_edge(&edge);
    pthread_mutex_unlock(&g_lock);
    signal_fd();
}

void gpio_mock_inject_press(int line, bool long_press) {
    uint64_t now_ns = time_hal_now_ns();

    /* Simulate press (falling edge for active-low) */
    gpio_mock_inject_edge(line, 1, now_ns);

    if (!long_press) {
        /* Simulate short release (rising edge) */
        gpio_mock_inject_edge(line, 0, now_ns + 100 * 1000000ULL);
    }
}

void gpio_mock_clear(void) {
    pthread_mutex_lock(&g_lock);
    reset_state();
#if USE_TIMERFD
    update_long_press_timer_locked();
#endif
    drain_fd();
    pthread_mutex_unlock(&g_lock);
}

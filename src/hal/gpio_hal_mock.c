#include "gpio_hal.h"

#include <errno.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#if defined(__linux__)
#include <sys/eventfd.h>
#define USE_EVENTFD 1
#else
#define USE_EVENTFD 0
#endif
#include <poll.h>
#include <unistd.h>

#include "hal/time_hal.h"

typedef enum {
    EDGE_RISING = 0,
    EDGE_FALLING = 1
} edge_type_t;

typedef struct {
    int line;
    edge_type_t type;
    uint64_t timestamp_ns;
} gpio_mock_edge_t;

#define NUM_BUTTONS 3
#define MAX_EDGE_EVENTS 64
#define MAX_PENDING_EVENTS 32
#define LONG_PRESS_MS 600
#define DEBOUNCE_MS 30

static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
// Linux 用 eventfd，其它平台用 pipe 模拟可 poll 的唤醒。
static int g_notify_fd = -1;
static int g_notify_write_fd = -1;

static gpio_mock_edge_t g_edges[MAX_EDGE_EVENTS];
static size_t g_edge_head = 0;
static size_t g_edge_tail = 0;
static size_t g_edge_count = 0;

static gpio_event_t g_pending[MAX_PENDING_EVENTS];
static size_t g_pending_head = 0;
static size_t g_pending_tail = 0;
static size_t g_pending_count = 0;

static uint64_t g_press_time_ms[NUM_BUTTONS];
static uint64_t g_last_edge_ms[NUM_BUTTONS];
static bool g_pressed[NUM_BUTTONS];

static int g_line_values[NUM_BUTTONS] = {1, 1, 1};
static int g_pressed_level = 0;
static bool g_hw_debounce_supported = true;
static bool g_use_soft_debounce = false;

static void gpio_mock_reset_state(void) {
    memset(g_press_time_ms, 0, sizeof(g_press_time_ms));
    memset(g_last_edge_ms, 0, sizeof(g_last_edge_ms));
    memset(g_pressed, 0, sizeof(g_pressed));
    g_edge_head = g_edge_tail = g_edge_count = 0;
    g_pending_head = g_pending_tail = g_pending_count = 0;
}

static void gpio_mock_signal(void) {
    if (g_notify_write_fd >= 0) {
#if USE_EVENTFD
        uint64_t v = 1;
        write(g_notify_write_fd, &v, sizeof(v));
#else
        uint8_t v = 1;
        write(g_notify_write_fd, &v, sizeof(v));
#endif
    }
}

static void gpio_mock_drain_eventfd(void) {
    if (g_notify_fd < 0) {
        return;
    }
#if USE_EVENTFD
    uint64_t v = 0;
    while (read(g_notify_fd, &v, sizeof(v)) > 0) {
        if (v == 0) {
            break;
        }
    }
#else
    uint8_t v = 0;
    while (read(g_notify_fd, &v, sizeof(v)) > 0) {
        if (v == 0) {
            break;
        }
    }
#endif
}

static void gpio_mock_push_edge(const gpio_mock_edge_t *edge) {
    if (g_edge_count >= MAX_EDGE_EVENTS) {
        g_edge_head = (g_edge_head + 1) % MAX_EDGE_EVENTS;
        g_edge_count--;
    }
    g_edges[g_edge_tail] = *edge;
    g_edge_tail = (g_edge_tail + 1) % MAX_EDGE_EVENTS;
    g_edge_count++;
}

static bool gpio_mock_pop_edge(gpio_mock_edge_t *edge) {
    if (g_edge_count == 0) {
        return false;
    }
    *edge = g_edges[g_edge_head];
    g_edge_head = (g_edge_head + 1) % MAX_EDGE_EVENTS;
    g_edge_count--;
    return true;
}

static void gpio_mock_push_pending(const gpio_event_t *event) {
    if (g_pending_count >= MAX_PENDING_EVENTS) {
        g_pending_head = (g_pending_head + 1) % MAX_PENDING_EVENTS;
        g_pending_count--;
    }
    g_pending[g_pending_tail] = *event;
    g_pending_tail = (g_pending_tail + 1) % MAX_PENDING_EVENTS;
    g_pending_count++;
}

static bool gpio_mock_pop_pending(gpio_event_t *event) {
    if (g_pending_count == 0) {
        return false;
    }
    *event = g_pending[g_pending_head];
    g_pending_head = (g_pending_head + 1) % MAX_PENDING_EVENTS;
    g_pending_count--;
    return true;
}

static gpio_event_type_t to_button_event(int line, bool long_press) {
    if (line == 0) {
        return long_press ? GPIO_EVT_BTN_K1_LONG : GPIO_EVT_BTN_K1_SHORT;
    }
    if (line == 1) {
        return long_press ? GPIO_EVT_BTN_K2_LONG : GPIO_EVT_BTN_K2_SHORT;
    }
    return long_press ? GPIO_EVT_BTN_K3_LONG : GPIO_EVT_BTN_K3_SHORT;
}

static int detect_pressed_level(void) {
    int zeros = 0;
    int ones = 0;
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (g_line_values[i] == 0) {
            zeros++;
        } else {
            ones++;
        }
    }
    return (zeros >= ones) ? 1 : 0;
}

static void process_edge_event(const gpio_mock_edge_t *edge) {
    uint64_t now_ms = edge->timestamp_ns / 1000000ULL;
    int line = edge->line;
    if (line < 0 || line >= NUM_BUTTONS) {
        return;
    }

    if (g_use_soft_debounce && g_last_edge_ms[line] != 0 &&
        now_ms - g_last_edge_ms[line] < DEBOUNCE_MS) {
        return;
    }
    g_last_edge_ms[line] = now_ms;

    int new_value = (edge->type == EDGE_FALLING) ? 0 : 1;
    g_line_values[line] = new_value;
    bool is_pressed = (new_value == g_pressed_level);

    if (is_pressed) {
        g_pressed[line] = true;
        g_press_time_ms[line] = now_ms;
        return;
    }

    if (g_pressed[line]) {
        g_pressed[line] = false;
        uint64_t elapsed = now_ms - g_press_time_ms[line];
        gpio_event_t evt = {
            .type = to_button_event(line, elapsed >= LONG_PRESS_MS),
            .line = (uint8_t)line,
            .timestamp_ns = edge->timestamp_ns
        };
        gpio_mock_push_pending(&evt);
    }
}

int gpio_hal_mock_init(void) {
    pthread_mutex_lock(&g_lock);
    gpio_mock_reset_state();
    g_pressed_level = detect_pressed_level();
    g_use_soft_debounce = !g_hw_debounce_supported;
    if (g_notify_fd >= 0) {
        close(g_notify_fd);
        g_notify_fd = -1;
    }
    if (g_notify_write_fd >= 0 && g_notify_write_fd != g_notify_fd) {
        close(g_notify_write_fd);
        g_notify_write_fd = -1;
    }
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
    pthread_mutex_unlock(&g_lock);
    return (g_notify_fd >= 0) ? 0 : -1;
}

void gpio_hal_mock_cleanup(void) {
    pthread_mutex_lock(&g_lock);
    if (g_notify_fd >= 0) {
        close(g_notify_fd);
        g_notify_fd = -1;
    }
    if (g_notify_write_fd >= 0 && g_notify_write_fd != g_notify_fd) {
        close(g_notify_write_fd);
        g_notify_write_fd = -1;
    }
    gpio_mock_reset_state();
    pthread_mutex_unlock(&g_lock);
}

int gpio_hal_mock_wait_event(int timeout_ms, gpio_event_t *event) {
    if (!event) {
        return -1;
    }

    uint64_t start_ms = time_hal_now_ms();
    for (;;) {
        pthread_mutex_lock(&g_lock);
        if (gpio_mock_pop_pending(event)) {
            pthread_mutex_unlock(&g_lock);
            return 1;
        }
        pthread_mutex_unlock(&g_lock);

        if (g_notify_fd < 0) {
            return -1;
        }

        uint64_t now_ms = time_hal_now_ms();
        int effective_timeout = timeout_ms;
        if (timeout_ms >= 0) {
            uint64_t elapsed_total = now_ms - start_ms;
            if (elapsed_total >= (uint64_t)timeout_ms) {
                return 0;
            }
            effective_timeout = (int)((uint64_t)timeout_ms - elapsed_total);
        }

        struct pollfd pfd = {.fd = g_notify_fd, .events = POLLIN};
        int ret = poll(&pfd, 1, effective_timeout);
        if (ret == 0) {
            return 0;
        }
        if (ret < 0) {
            return -1;
        }

        gpio_mock_drain_eventfd();

        pthread_mutex_lock(&g_lock);
        gpio_mock_edge_t edge;
        while (gpio_mock_pop_edge(&edge)) {
            process_edge_event(&edge);
        }
        if (gpio_mock_pop_pending(event)) {
            pthread_mutex_unlock(&g_lock);
            return 1;
        }
        pthread_mutex_unlock(&g_lock);
    }
}

int gpio_hal_mock_get_fd(void) {
    return g_notify_fd;
}

static const gpio_hal_ops_t g_gpio_hal_mock_ops = {
    .init = gpio_hal_mock_init,
    .cleanup = gpio_hal_mock_cleanup,
    .wait_event = gpio_hal_mock_wait_event,
    .get_fd = gpio_hal_mock_get_fd
};

const gpio_hal_ops_t *gpio_hal = &g_gpio_hal_mock_ops;

void gpio_mock_inject_edge(int line, edge_type_t type, uint64_t timestamp_ns) {
    gpio_mock_edge_t edge = {.line = line, .type = type, .timestamp_ns = timestamp_ns};
    pthread_mutex_lock(&g_lock);
    gpio_mock_push_edge(&edge);
    pthread_mutex_unlock(&g_lock);
    gpio_mock_signal();
}

void gpio_mock_set_line_value(int line, int value) {
    if (line < 0 || line >= NUM_BUTTONS) {
        return;
    }
    pthread_mutex_lock(&g_lock);
    g_line_values[line] = value ? 1 : 0;
    pthread_mutex_unlock(&g_lock);
}

void gpio_mock_clear_events(void) {
    pthread_mutex_lock(&g_lock);
    g_edge_head = g_edge_tail = g_edge_count = 0;
    g_pending_head = g_pending_tail = g_pending_count = 0;
    pthread_mutex_unlock(&g_lock);
    gpio_mock_drain_eventfd();
}

int gpio_mock_get_pending_count(void) {
    pthread_mutex_lock(&g_lock);
    int count = (int)g_edge_count;
    pthread_mutex_unlock(&g_lock);
    return count;
}

int gpio_mock_get_fd(void) {
    return g_notify_fd;
}

void gpio_mock_set_debounce_supported(bool supported) {
    pthread_mutex_lock(&g_lock);
    g_hw_debounce_supported = supported;
    g_use_soft_debounce = !supported;
    pthread_mutex_unlock(&g_lock);
}

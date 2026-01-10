/*
 * GPIO HAL implementation using libgpiod v2
 *
 * For NanoHat OLED buttons on NanoPi NEO series.
 * Supports hardware debounce with software fallback.
 */
#include "gpio_hal.h"

#include <errno.h>
#include <gpiod.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/timerfd.h>
#include <time.h>
#include <unistd.h>

#include "time_hal.h"

#ifdef GPIO_DEBUG
#define GPIO_LOG(...) fprintf(stderr, "[gpio] " __VA_ARGS__)
#else
#define GPIO_LOG(...) do {} while (0)
#endif

#ifndef GPIOCHIP_PATH
#define GPIOCHIP_PATH "/dev/gpiochip1"
#endif

#ifndef BTN_OFFSETS
#define BTN_OFFSETS 0, 2, 3
#endif

#define MAX_PENDING_EVENTS 32

/* Internal event queue */
typedef struct {
    gpio_event_t events[MAX_PENDING_EVENTS];
    size_t head;
    size_t tail;
    size_t count;
} event_queue_t;

/* Static state */
static struct gpiod_chip *g_chip = NULL;
static struct gpiod_line_request *g_request = NULL;
static struct gpiod_edge_event_buffer *g_event_buf = NULL;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;
static int g_timer_fd = -1;
static int g_gpiod_fd = -1;

static const unsigned int g_btn_offsets[GPIO_NUM_BUTTONS] = {BTN_OFFSETS};
static int g_pressed_level = 0;
static bool g_use_soft_debounce = false;

static uint64_t g_press_time_ms[GPIO_NUM_BUTTONS];
static uint64_t g_last_edge_ms[GPIO_NUM_BUTTONS];
static bool g_pressed[GPIO_NUM_BUTTONS];
static bool g_long_sent[GPIO_NUM_BUTTONS];
static event_queue_t g_pending;

/* Queue operations */
static void pending_push(const gpio_event_t *event) {
    if (g_pending.count >= MAX_PENDING_EVENTS) {
        g_pending.head = (g_pending.head + 1) % MAX_PENDING_EVENTS;
        g_pending.count--;
    }
    g_pending.events[g_pending.tail] = *event;
    g_pending.tail = (g_pending.tail + 1) % MAX_PENDING_EVENTS;
    g_pending.count++;
}

static bool pending_pop(gpio_event_t *event) {
    if (g_pending.count == 0) {
        return false;
    }
    *event = g_pending.events[g_pending.head];
    g_pending.head = (g_pending.head + 1) % MAX_PENDING_EVENTS;
    g_pending.count--;
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
        int val = gpiod_line_request_get_value(g_request, g_btn_offsets[i]);
        if (val < 0) return -1;
        if (val == 0) zeros++; else ones++;
    }
    /* Idle level is majority; pressed level is opposite */
    int idle_level = (zeros >= ones) ? 0 : 1;
    GPIO_LOG("detect pressed level: zeros=%d ones=%d -> pressed=%d\n",
             zeros, ones, 1 - idle_level);
    return 1 - idle_level;
}

static void reset_state(void) {
    memset(g_press_time_ms, 0, sizeof(g_press_time_ms));
    memset(g_last_edge_ms, 0, sizeof(g_last_edge_ms));
    memset(g_pressed, 0, sizeof(g_pressed));
    memset(g_long_sent, 0, sizeof(g_long_sent));
    memset(&g_pending, 0, sizeof(g_pending));
}

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
            next_delay_ms = 1; /* fire asap */
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
            pending_push(&evt);
            GPIO_LOG("event: line=%d type=%d long_press\n", i, evt.type);
        }
    }

    update_long_press_timer_locked();
}

static void process_edge_event(struct gpiod_edge_event *event) {
    unsigned int offset = gpiod_edge_event_get_line_offset(event);
    enum gpiod_edge_event_type type = gpiod_edge_event_get_event_type(event);
    uint64_t timestamp_ns = gpiod_edge_event_get_timestamp_ns(event);
    uint64_t now_ms = timestamp_ns / 1000000ULL;

    /* Find button index */
    int line = -1;
    for (int i = 0; i < GPIO_NUM_BUTTONS; i++) {
        if (g_btn_offsets[i] == offset) {
            line = i;
            break;
        }
    }
    if (line < 0) return;

    /* Software debounce if hardware debounce not available */
    if (g_use_soft_debounce && g_last_edge_ms[line] != 0 &&
        now_ms - g_last_edge_ms[line] < GPIO_DEBOUNCE_MS) {
        return;
    }
    g_last_edge_ms[line] = now_ms;

    int new_value = (type == GPIOD_EDGE_EVENT_FALLING_EDGE) ? 0 : 1;
    bool is_pressed = (new_value == g_pressed_level);

    GPIO_LOG("edge offset=%u line=%d value=%d is_pressed=%d\n",
             offset, line, new_value, (int)is_pressed);

    bool use_longpress_timer = (g_timer_fd >= 0);

    if (is_pressed) {
        /* Button pressed - record time */
        g_pressed[line] = true;
        g_press_time_ms[line] = now_ms;
        g_long_sent[line] = false;
        if (use_longpress_timer) {
            update_long_press_timer_locked();
        }
        return;
    }

    /* Button released - emit event */
    if (g_pressed[line]) {
        g_pressed[line] = false;
        if (!use_longpress_timer) {
            uint64_t elapsed = now_ms - g_press_time_ms[line];
            gpio_event_t evt = {
                .type = to_button_event(line, elapsed >= GPIO_LONG_PRESS_MS),
                .line = (uint8_t)line,
                .timestamp_ns = timestamp_ns
            };
            pending_push(&evt);
            GPIO_LOG("event: line=%d type=%d elapsed=%llu ms\n",
                     line, evt.type, (unsigned long long)elapsed);
        } else {
            bool long_sent = g_long_sent[line];
            g_long_sent[line] = false;
            if (!long_sent) {
                gpio_event_t evt = {
                    .type = to_button_event(line, false),
                    .line = (uint8_t)line,
                    .timestamp_ns = timestamp_ns
                };
                pending_push(&evt);
                GPIO_LOG("event: line=%d type=%d short_press\n", line, evt.type);
            }
            update_long_press_timer_locked();
        }
    }
}

static struct gpiod_line_request *request_lines(bool with_debounce) {
    struct gpiod_line_settings *settings = gpiod_line_settings_new();
    struct gpiod_line_config *line_config = gpiod_line_config_new();
    struct gpiod_request_config *req_config = gpiod_request_config_new();

    if (!settings || !line_config || !req_config) {
        goto cleanup;
    }

    gpiod_line_settings_set_direction(settings, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_edge_detection(settings, GPIOD_LINE_EDGE_BOTH);
    if (with_debounce) {
        gpiod_line_settings_set_debounce_period_us(settings, GPIO_DEBOUNCE_MS * 1000);
    }

    gpiod_line_config_add_line_settings(line_config, g_btn_offsets, GPIO_NUM_BUTTONS, settings);
    gpiod_request_config_set_consumer(req_config, "nanohat-oled");

    struct gpiod_line_request *req = gpiod_chip_request_lines(g_chip, req_config, line_config);

    gpiod_request_config_free(req_config);
    gpiod_line_config_free(line_config);
    gpiod_line_settings_free(settings);
    return req;

cleanup:
    if (req_config) gpiod_request_config_free(req_config);
    if (line_config) gpiod_line_config_free(line_config);
    if (settings) gpiod_line_settings_free(settings);
    return NULL;
}

/* HAL Interface Implementation */

static int libgpiod_init(void) {
    pthread_mutex_lock(&g_lock);
    reset_state();

    g_chip = gpiod_chip_open(GPIOCHIP_PATH);
    if (!g_chip) {
        GPIO_LOG("gpiod_chip_open(\"%s\") failed: %s\n", GPIOCHIP_PATH, strerror(errno));
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    /* Try hardware debounce first, fall back to software */
    g_request = request_lines(true);
    if (!g_request && (errno == ENOTSUP || errno == EINVAL)) {
        g_request = request_lines(false);
        g_use_soft_debounce = true;
    } else {
        g_use_soft_debounce = false;
    }

    if (!g_request) {
        GPIO_LOG("request_lines failed: %s\n", strerror(errno));
        gpiod_chip_close(g_chip);
        g_chip = NULL;
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    g_event_buf = gpiod_edge_event_buffer_new(16);
    if (!g_event_buf) {
        gpiod_line_request_release(g_request);
        g_request = NULL;
        gpiod_chip_close(g_chip);
        g_chip = NULL;
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    g_gpiod_fd = gpiod_line_request_get_fd(g_request);
    g_timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (g_timer_fd < 0) {
        gpiod_edge_event_buffer_free(g_event_buf);
        g_event_buf = NULL;
        gpiod_line_request_release(g_request);
        g_request = NULL;
        gpiod_chip_close(g_chip);
        g_chip = NULL;
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    g_pressed_level = detect_pressed_level();
    if (g_pressed_level < 0) {
        gpiod_edge_event_buffer_free(g_event_buf);
        g_event_buf = NULL;
        gpiod_line_request_release(g_request);
        g_request = NULL;
        close(g_timer_fd);
        g_timer_fd = -1;
        gpiod_chip_close(g_chip);
        g_chip = NULL;
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    GPIO_LOG("init ok: fd=%d soft_debounce=%d\n",
             g_gpiod_fd, g_use_soft_debounce);
    pthread_mutex_unlock(&g_lock);
    return 0;
}

static void libgpiod_cleanup(void) {
    pthread_mutex_lock(&g_lock);
    if (g_event_buf) {
        gpiod_edge_event_buffer_free(g_event_buf);
        g_event_buf = NULL;
    }
    if (g_timer_fd >= 0) {
        close(g_timer_fd);
        g_timer_fd = -1;
    }
    if (g_request) {
        gpiod_line_request_release(g_request);
        g_request = NULL;
    }
    if (g_chip) {
        gpiod_chip_close(g_chip);
        g_chip = NULL;
    }
    g_gpiod_fd = -1;
    reset_state();
    pthread_mutex_unlock(&g_lock);
}

static int libgpiod_get_fd(void) {
    if (!g_request) {
        return -1;
    }
    return g_gpiod_fd;
}

static int libgpiod_get_timer_fd(void) {
    return g_timer_fd;
}

static int libgpiod_read_event(gpio_event_t *event) {
    if (!event || !g_request) {
        return -1;
    }

    pthread_mutex_lock(&g_lock);

    /* Check for pending events first */
    if (pending_pop(event)) {
        pthread_mutex_unlock(&g_lock);
        return 1;
    }

    /* Drain timerfd if needed and emit long-press events */
    if (g_timer_fd >= 0) {
        uint64_t expirations;
        while (read(g_timer_fd, &expirations, sizeof(expirations)) > 0) {
            /* drain */
        }
        handle_long_press_timer_locked();
    }

    if (pending_pop(event)) {
        pthread_mutex_unlock(&g_lock);
        return 1;
    }

    /* Read edge events from fd (non-blocking since fd should be readable) */
    int num = gpiod_line_request_read_edge_events(g_request, g_event_buf, 16);
    if (num < 0) {
        /* EAGAIN/EWOULDBLOCK means no events available - not an error */
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            pthread_mutex_unlock(&g_lock);
            return 0;
        }
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    /* Process all edge events */
    for (int i = 0; i < num; i++) {
        struct gpiod_edge_event *edge = gpiod_edge_event_buffer_get_event(g_event_buf, i);
        process_edge_event(edge);
    }

    /* Return next pending event if available */
    bool has_event = pending_pop(event);
    pthread_mutex_unlock(&g_lock);
    return has_event ? 1 : 0;
}

/* HAL operations table */
static const gpio_hal_ops_t libgpiod_ops = {
    .init = libgpiod_init,
    .cleanup = libgpiod_cleanup,
    .get_fd = libgpiod_get_fd,
    .get_timer_fd = libgpiod_get_timer_fd,
    .read_event = libgpiod_read_event,
};

const gpio_hal_ops_t *gpio_hal = &libgpiod_ops;

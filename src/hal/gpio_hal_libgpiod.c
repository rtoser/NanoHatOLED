#include "gpio_hal.h"

#include <errno.h>
#include <gpiod.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "hal/time_hal.h"

#ifndef GPIOCHIP_PATH
#define GPIOCHIP_PATH "/dev/gpiochip0"
#endif

#ifndef BTN_OFFSETS
#define BTN_OFFSETS 0, 2, 3
#endif

#define NUM_BUTTONS 3
#define LONG_PRESS_MS 600
#define DEBOUNCE_MS 30
#define MAX_PENDING_EVENTS 32

typedef struct {
    gpio_event_t events[MAX_PENDING_EVENTS];
    size_t head;
    size_t tail;
    size_t count;
} event_queue_t;

static struct gpiod_chip *g_chip = NULL;
static struct gpiod_line_request *g_request = NULL;
static struct gpiod_edge_event_buffer *g_event_buf = NULL;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

static const unsigned int g_btn_offsets[NUM_BUTTONS] = {BTN_OFFSETS};
static int g_pressed_level = 0;
static bool g_use_soft_debounce = false;

static uint64_t g_press_time_ms[NUM_BUTTONS];
static uint64_t g_last_edge_ms[NUM_BUTTONS];
static bool g_pressed[NUM_BUTTONS];
static bool g_long_reported[NUM_BUTTONS];
static event_queue_t g_pending;

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
        int val = gpiod_line_request_get_value(g_request, g_btn_offsets[i]);
        if (val == 0) {
            zeros++;
        } else {
            ones++;
        }
    }
    return (zeros >= ones) ? 1 : 0;
}

static void reset_state(void) {
    memset(g_press_time_ms, 0, sizeof(g_press_time_ms));
    memset(g_last_edge_ms, 0, sizeof(g_last_edge_ms));
    memset(g_pressed, 0, sizeof(g_pressed));
    memset(g_long_reported, 0, sizeof(g_long_reported));
    memset(&g_pending, 0, sizeof(g_pending));
}

static void process_edge_event(struct gpiod_edge_event *event) {
    unsigned int offset = gpiod_edge_event_get_line_offset(event);
    enum gpiod_edge_event_type type = gpiod_edge_event_get_event_type(event);
    uint64_t timestamp_ns = gpiod_edge_event_get_timestamp_ns(event);
    uint64_t now_ms = timestamp_ns / 1000000ULL;

    int line = -1;
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (g_btn_offsets[i] == offset) {
            line = i;
            break;
        }
    }
    if (line < 0) {
        return;
    }

    if (g_use_soft_debounce && g_last_edge_ms[line] != 0 &&
        now_ms - g_last_edge_ms[line] < DEBOUNCE_MS) {
        return;
    }
    g_last_edge_ms[line] = now_ms;

    int new_value = (type == GPIOD_EDGE_EVENT_FALLING_EDGE) ? 0 : 1;
    bool is_pressed = (new_value == g_pressed_level);

    if (is_pressed) {
        g_pressed[line] = true;
        g_press_time_ms[line] = now_ms;
        g_long_reported[line] = false;
        return;
    }

    if (g_pressed[line]) {
        g_pressed[line] = false;
        if (!g_long_reported[line]) {
            gpio_event_t evt = {
                .type = to_button_event(line, false),
                .line = (uint8_t)line,
                .timestamp_ns = timestamp_ns
            };
            pending_push(&evt);
        }
    }
}

static bool check_long_press(gpio_event_t *event) {
    uint64_t now_ms = time_hal_now_ms();
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (g_pressed[i] && !g_long_reported[i]) {
            if (now_ms - g_press_time_ms[i] >= LONG_PRESS_MS) {
                g_long_reported[i] = true;
                event->type = to_button_event(i, true);
                event->line = (uint8_t)i;
                event->timestamp_ns = time_hal_now_ns();
                return true;
            }
        }
    }
    return false;
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
        gpiod_line_settings_set_debounce_period_us(settings, DEBOUNCE_MS * 1000);
    }

    gpiod_line_config_add_line_settings(line_config, g_btn_offsets, NUM_BUTTONS, settings);
    gpiod_request_config_set_consumer(req_config, "nanohat-oled");

    struct gpiod_line_request *req = gpiod_chip_request_lines(g_chip, req_config, line_config);

    gpiod_request_config_free(req_config);
    gpiod_line_config_free(line_config);
    gpiod_line_settings_free(settings);
    return req;

cleanup:
    if (req_config) {
        gpiod_request_config_free(req_config);
    }
    if (line_config) {
        gpiod_line_config_free(line_config);
    }
    if (settings) {
        gpiod_line_settings_free(settings);
    }
    return NULL;
}

int gpio_hal_libgpiod_init(void) {
    pthread_mutex_lock(&g_lock);
    reset_state();
    g_chip = gpiod_chip_open(GPIOCHIP_PATH);
    if (!g_chip) {
        pthread_mutex_unlock(&g_lock);
        return -1;
    }

    // 优先尝试硬件去抖，不支持则回退到软件去抖。
    g_request = request_lines(true);
    if (!g_request && (errno == ENOTSUP || errno == EINVAL)) {
        g_request = request_lines(false);
        g_use_soft_debounce = true;
    } else {
        g_use_soft_debounce = false;
    }

    if (!g_request) {
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

    g_pressed_level = detect_pressed_level();
    pthread_mutex_unlock(&g_lock);
    return 0;
}

void gpio_hal_libgpiod_cleanup(void) {
    pthread_mutex_lock(&g_lock);
    if (g_event_buf) {
        gpiod_edge_event_buffer_free(g_event_buf);
        g_event_buf = NULL;
    }
    if (g_request) {
        gpiod_line_request_release(g_request);
        g_request = NULL;
    }
    if (g_chip) {
        gpiod_chip_close(g_chip);
        g_chip = NULL;
    }
    reset_state();
    pthread_mutex_unlock(&g_lock);
}

int gpio_hal_libgpiod_wait_event(int timeout_ms, gpio_event_t *event) {
    if (!event || !g_request) {
        return -1;
    }

    pthread_mutex_lock(&g_lock);
    if (pending_pop(event)) {
        pthread_mutex_unlock(&g_lock);
        return 1;
    }
    pthread_mutex_unlock(&g_lock);

    uint64_t now_ms = time_hal_now_ms();
    int effective_timeout = timeout_ms;

    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (g_pressed[i] && !g_long_reported[i]) {
            uint64_t elapsed = now_ms - g_press_time_ms[i];
            if (elapsed < LONG_PRESS_MS) {
                int remaining = (int)(LONG_PRESS_MS - elapsed);
                if (effective_timeout < 0 || remaining < effective_timeout) {
                    effective_timeout = remaining;
                }
            } else {
                g_long_reported[i] = true;
                event->type = to_button_event(i, true);
                event->line = (uint8_t)i;
                event->timestamp_ns = time_hal_now_ns();
                pthread_mutex_unlock(&g_lock);
                return 1;
            }
        }
    }
    pthread_mutex_unlock(&g_lock);

    int64_t timeout_ns = (effective_timeout < 0) ? -1 : (int64_t)effective_timeout * 1000000;
    // wait_event 走阻塞等待；主线程可改用 get_fd + poll。
    int ret = gpiod_line_request_wait_edge_events(g_request, timeout_ns);
    if (ret <= 0) {
        if (ret == 0 && check_long_press(event)) {
            return 1;
        }
        return ret;
    }

    int num = gpiod_line_request_read_edge_events(g_request, g_event_buf, 16);
    pthread_mutex_lock(&g_lock);
    for (int i = 0; i < num; i++) {
        struct gpiod_edge_event *edge = gpiod_edge_event_buffer_get_event(g_event_buf, i);
        process_edge_event(edge);
    }
    if (pending_pop(event)) {
        pthread_mutex_unlock(&g_lock);
        return 1;
    }
    pthread_mutex_unlock(&g_lock);

    if (check_long_press(event)) {
        return 1;
    }

    return 0;
}

int gpio_hal_libgpiod_get_fd(void) {
    if (!g_request) {
        return -1;
    }
    // 返回可被 poll 监听的 fd，用于主线程统一事件循环。
    return gpiod_line_request_get_fd(g_request);
}

static const gpio_hal_ops_t g_gpio_hal_ops = {
    .init = gpio_hal_libgpiod_init,
    .cleanup = gpio_hal_libgpiod_cleanup,
    .wait_event = gpio_hal_libgpiod_wait_event,
    .get_fd = gpio_hal_libgpiod_get_fd
};

const gpio_hal_ops_t *gpio_hal = &g_gpio_hal_ops;

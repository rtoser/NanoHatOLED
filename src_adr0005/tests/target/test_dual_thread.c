#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "event_loop.h"
#include "event_queue.h"
#include "ui_controller.h"
#include "ui_thread.h"
#include "hal/display_hal.h"
#include "hal/gpio_hal.h"

#ifndef TEST_IDLE_TIMEOUT_MS
#define TEST_IDLE_TIMEOUT_MS 5000
#endif

#define TEST_FIRST_BUTTON_TIMEOUT_MS 10000
#define TEST_WAKE_TIMEOUT_MS 10000
#define TEST_SLEEP_GRACE_MS 2000

#define UI_ANIM_TICK_MS 50
#define UI_IDLE_TICK_MS 1000
#define UI_DEFAULT_ANIM_TICKS 10

const display_hal_ops_t *display_hal = NULL;

typedef struct {
    event_loop_t *loop;
    ui_controller_t controller;
    int tick_ms;
    bool tick_active;
    int anim_ticks_left;
    _Atomic uint32_t button_events;
    _Atomic bool saw_sleep;
    _Atomic bool saw_wake;
    _Atomic bool failed;
} ui_test_ctx_t;

static event_loop_t *g_loop = NULL;

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
}

static void handle_sigint(int sig) {
    (void)sig;
    if (g_loop) {
        event_loop_request_shutdown(g_loop);
    }
}

static const char *event_name(app_event_type_t type) {
    switch (type) {
        case EVT_BTN_K1_SHORT: return "K1_SHORT";
        case EVT_BTN_K2_SHORT: return "K2_SHORT";
        case EVT_BTN_K3_SHORT: return "K3_SHORT";
        case EVT_BTN_K1_LONG: return "K1_LONG";
        case EVT_BTN_K2_LONG: return "K2_LONG";
        case EVT_BTN_K3_LONG: return "K3_LONG";
        case EVT_TICK: return "TICK";
        case EVT_SHUTDOWN: return "SHUTDOWN";
        default: return "NONE";
    }
}

static bool is_button_event(app_event_type_t type) {
    return type == EVT_BTN_K1_SHORT || type == EVT_BTN_K2_SHORT || type == EVT_BTN_K3_SHORT ||
           type == EVT_BTN_K1_LONG || type == EVT_BTN_K2_LONG || type == EVT_BTN_K3_LONG;
}

static void ui_test_apply_tick(ui_test_ctx_t *ctx, int tick_ms) {
    if (!ctx || !ctx->loop) {
        return;
    }
    if (tick_ms == ctx->tick_ms) {
        return;
    }
    event_loop_request_tick(ctx->loop, tick_ms);
    ctx->tick_ms = tick_ms;
}

static void ui_test_stop_tick(ui_test_ctx_t *ctx) {
    if (!ctx || !ctx->tick_active) {
        return;
    }
    ui_test_apply_tick(ctx, 0);
    ctx->tick_active = false;
    ctx->anim_ticks_left = 0;
    printf("[tick] stop\n");
}

static void ui_test_start_anim_tick(ui_test_ctx_t *ctx) {
    if (!ctx) {
        return;
    }
    ctx->tick_active = true;
    ctx->anim_ticks_left = UI_DEFAULT_ANIM_TICKS;
    ui_test_apply_tick(ctx, UI_ANIM_TICK_MS);
    printf("[tick] anim %dms\n", UI_ANIM_TICK_MS);
}

static void ui_test_switch_idle_tick(ui_test_ctx_t *ctx) {
    if (!ctx) {
        return;
    }
    ctx->tick_active = true;
    ctx->anim_ticks_left = 0;
    ui_test_apply_tick(ctx, UI_IDLE_TICK_MS);
    printf("[tick] idle %dms\n", UI_IDLE_TICK_MS);
}

static void ui_test_handle(const app_event_t *event, void *user) {
    ui_test_ctx_t *ctx = (ui_test_ctx_t *)user;
    if (!ctx || !event) {
        return;
    }

    bool prev_power = ctx->controller.power_on;
    ui_controller_handle_event(&ctx->controller, event);
    ui_controller_render(&ctx->controller);

    if (event->type == EVT_SHUTDOWN) {
        ui_test_stop_tick(ctx);
        return;
    }

    if (!ctx->controller.power_on) {
        ui_test_stop_tick(ctx);
    } else if (event->type == EVT_TICK && ctx->tick_active) {
        uint32_t step = event->data ? event->data : 1;
        if (ctx->anim_ticks_left > 0) {
            if ((int)step >= ctx->anim_ticks_left) {
                ctx->anim_ticks_left = 0;
                ui_test_switch_idle_tick(ctx);
            } else {
                ctx->anim_ticks_left -= (int)step;
            }
        }
    } else if (is_button_event(event->type)) {
        ui_test_start_anim_tick(ctx);
        atomic_fetch_add(&ctx->button_events, 1);
    }

    if (prev_power && !ctx->controller.power_on) {
        atomic_store(&ctx->saw_sleep, true);
        printf("[state] auto sleep\n");
    }
    if (!prev_power && ctx->controller.power_on) {
        atomic_store(&ctx->saw_wake, true);
        printf("[state] wake\n");
    }

    if (is_button_event(event->type)) {
        printf("[event] %s line=%u\n", event_name(event->type), (unsigned)event->line);
    }
}

static void *monitor_main(void *arg) {
    ui_test_ctx_t *ctx = (ui_test_ctx_t *)arg;
    uint64_t start = now_ms();

    printf("请在 %d ms 内按任意键，确认事件链路...\n", TEST_FIRST_BUTTON_TIMEOUT_MS);
    while (now_ms() - start < TEST_FIRST_BUTTON_TIMEOUT_MS) {
        if (atomic_load(&ctx->button_events) > 0) {
            break;
        }
        usleep(100 * 1000);
    }
    if (atomic_load(&ctx->button_events) == 0) {
        printf("FAIL: 未检测到按键事件\n");
        atomic_store(&ctx->failed, true);
        event_loop_request_shutdown(ctx->loop);
        return NULL;
    }
    printf("PASS: 已检测到按键事件\n");

    printf("请保持无按键，等待自动息屏（%d ms）...\n", TEST_IDLE_TIMEOUT_MS);
    start = now_ms();
    while (now_ms() - start < (uint64_t)TEST_IDLE_TIMEOUT_MS + TEST_SLEEP_GRACE_MS) {
        if (atomic_load(&ctx->saw_sleep)) {
            break;
        }
        usleep(200 * 1000);
    }
    if (!atomic_load(&ctx->saw_sleep)) {
        printf("FAIL: 未触发自动息屏\n");
        atomic_store(&ctx->failed, true);
        event_loop_request_shutdown(ctx->loop);
        return NULL;
    }
    printf("PASS: 已触发自动息屏\n");

    printf("请按任意键唤醒（%d ms 内）...\n", TEST_WAKE_TIMEOUT_MS);
    start = now_ms();
    while (now_ms() - start < TEST_WAKE_TIMEOUT_MS) {
        if (atomic_load(&ctx->saw_wake)) {
            break;
        }
        usleep(200 * 1000);
    }
    if (!atomic_load(&ctx->saw_wake)) {
        printf("FAIL: 未检测到唤醒\n");
        atomic_store(&ctx->failed, true);
    } else {
        printf("PASS: 已唤醒\n");
    }

    event_loop_request_shutdown(ctx->loop);
    return NULL;
}

int main(void) {
    printf("=== Dual Thread Target Test ===\n");
    printf("提示：测试前请停止 nanohat-oled 服务，避免占用 GPIO。\n");

    if (!gpio_hal || gpio_hal->init() != 0) {
        printf("FAIL: gpio init\n");
        return 1;
    }

    event_queue_t queue;
    if (event_queue_init(&queue, 16) != 0) {
        printf("FAIL: queue init\n");
        gpio_hal->cleanup();
        return 1;
    }

    event_loop_t loop;
    if (event_loop_init(&loop, &queue) != 0) {
        printf("FAIL: event loop init\n");
        event_queue_destroy(&queue);
        gpio_hal->cleanup();
        return 1;
    }

    ui_test_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.loop = &loop;
    ui_controller_init(&ctx.controller);
    ctx.controller.idle_timeout_ms = TEST_IDLE_TIMEOUT_MS;
    ui_test_switch_idle_tick(&ctx);

    ui_thread_t ui_thread;
    if (ui_thread_start(&ui_thread, &queue, ui_test_handle, &ctx) != 0) {
        printf("FAIL: ui thread start\n");
        event_loop_cleanup(&loop);
        event_queue_destroy(&queue);
        gpio_hal->cleanup();
        return 1;
    }

    g_loop = &loop;
    signal(SIGINT, handle_sigint);

    pthread_t monitor;
    pthread_create(&monitor, NULL, monitor_main, &ctx);

    int rc = event_loop_run(&loop);
    if (rc != 0) {
        printf("FAIL: event loop run\n");
        atomic_store(&ctx.failed, true);
    }

    pthread_join(monitor, NULL);
    ui_thread_stop(&ui_thread);
    event_loop_cleanup(&loop);
    event_queue_destroy(&queue);
    gpio_hal->cleanup();

    if (atomic_load(&ctx.failed)) {
        printf("TEST FAILED\n");
        return 1;
    }

    printf("TEST PASSED\n");
    return 0;
}

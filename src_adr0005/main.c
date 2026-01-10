#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "event_loop.h"
#include "event_queue.h"
#include "ui_thread.h"
#include "task_queue.h"
#include "result_queue.h"
#include "ubus_thread.h"
#include "sys_status.h"
#include "hal/display_hal.h"
#include "hal/gpio_hal.h"
#include "hal/ubus_hal.h"

#define APP_NAME "nanohat-oled"

static event_loop_t g_loop;
static ubus_thread_t g_ubus;

static void handle_signal(int sig) {
    (void)sig;
    event_loop_request_shutdown(&g_loop);
}

static void setup_signals(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
}

static void cleanup_hal(void) {
    if (ubus_hal && ubus_hal->cleanup) {
        ubus_hal->cleanup();
    }
    if (display_hal && display_hal->cleanup) {
        display_hal->cleanup();
    }
    gpio_hal->cleanup();
}

int main(void) {
    printf("%s starting...\n", APP_NAME);

    if (!gpio_hal || gpio_hal->init() != 0) {
        printf("FAIL: gpio init\n");
        return 1;
    }

    if (display_hal && display_hal->init) {
        if (display_hal->init() != 0) {
            printf("FAIL: display init\n");
            gpio_hal->cleanup();
            return 1;
        }
    }

    if (ubus_hal && ubus_hal->init) {
        if (ubus_hal->init() != 0) {
            printf("WARN: ubus init failed, continuing without ubus\n");
        }
    }

    event_queue_t event_queue;
    if (event_queue_init(&event_queue, 32) != 0) {
        printf("FAIL: event queue init\n");
        cleanup_hal();
        return 1;
    }

    task_queue_t task_queue;
    if (task_queue_init(&task_queue, 16) != 0) {
        printf("FAIL: task queue init\n");
        event_queue_destroy(&event_queue);
        cleanup_hal();
        return 1;
    }

    result_queue_t result_queue;
    if (result_queue_init(&result_queue, 16) != 0) {
        printf("FAIL: result queue init\n");
        task_queue_destroy(&task_queue);
        event_queue_destroy(&event_queue);
        cleanup_hal();
        return 1;
    }

    /* Initialize sys_status for UI display */
    sys_status_t sys_status;
    memset(&sys_status, 0, sizeof(sys_status));
    sys_status_ctx_t *sys_status_ctx = sys_status_init(&task_queue, &result_queue);
    if (!sys_status_ctx) {
        printf("WARN: sys_status init failed, continuing without status\n");
    }

    if (event_loop_init(&g_loop, &event_queue) != 0) {
        printf("FAIL: event loop init\n");
        sys_status_cleanup(sys_status_ctx);
        result_queue_destroy(&result_queue);
        task_queue_destroy(&task_queue);
        event_queue_destroy(&event_queue);
        cleanup_hal();
        return 1;
    }

    if (ubus_thread_init(&g_ubus, &task_queue, &result_queue) != 0) {
        printf("FAIL: ubus thread init\n");
        event_loop_cleanup(&g_loop);
        sys_status_cleanup(sys_status_ctx);
        result_queue_destroy(&result_queue);
        task_queue_destroy(&task_queue);
        event_queue_destroy(&event_queue);
        cleanup_hal();
        return 1;
    }

    if (ubus_thread_start(&g_ubus) != 0) {
        printf("FAIL: ubus thread start\n");
        ubus_thread_destroy(&g_ubus);
        event_loop_cleanup(&g_loop);
        sys_status_cleanup(sys_status_ctx);
        result_queue_destroy(&result_queue);
        task_queue_destroy(&task_queue);
        event_queue_destroy(&event_queue);
        cleanup_hal();
        return 1;
    }

    ui_thread_t ui;
    if (ui_thread_start_default(&ui, &event_queue, &g_loop) != 0) {
        printf("FAIL: ui thread start\n");
        ubus_thread_stop(&g_ubus);
        ubus_thread_destroy(&g_ubus);
        event_loop_cleanup(&g_loop);
        sys_status_cleanup(sys_status_ctx);
        result_queue_destroy(&result_queue);
        task_queue_destroy(&task_queue);
        event_queue_destroy(&event_queue);
        cleanup_hal();
        return 1;
    }

    /* Set sys_status for UI thread */
    ui_thread_set_status(&ui, &sys_status, sys_status_ctx);

    printf("%s ready (3 threads)\n", APP_NAME);

    setup_signals();
    int rc = event_loop_run(&g_loop);
    if (rc != 0) {
        printf("FAIL: event loop run\n");
    }

    printf("%s shutting down...\n", APP_NAME);

    ui_thread_stop(&ui);
    task_queue_close(&task_queue);
    result_queue_close(&result_queue);
    ubus_thread_stop(&g_ubus);

    ubus_thread_destroy(&g_ubus);
    event_loop_cleanup(&g_loop);
    sys_status_cleanup(sys_status_ctx);
    result_queue_destroy(&result_queue);
    task_queue_destroy(&task_queue);
    event_queue_destroy(&event_queue);
    cleanup_hal();

    printf("%s exit\n", APP_NAME);
    return rc == 0 ? 0 : 1;
}

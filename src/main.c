#define _POSIX_C_SOURCE 200809L

#include <signal.h>
#include <stdio.h>
#include <string.h>

#include "event_loop.h"
#include "event_queue.h"
#include "ui_thread.h"
#include "hal/display_hal.h"
#include "hal/gpio_hal.h"

#define APP_NAME "nanohat-oled"

static event_loop_t g_loop;

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

    event_queue_t queue;
    if (event_queue_init(&queue, 32) != 0) {
        printf("FAIL: queue init\n");
        if (display_hal && display_hal->cleanup) {
            display_hal->cleanup();
        }
        gpio_hal->cleanup();
        return 1;
    }

    if (event_loop_init(&g_loop, &queue) != 0) {
        printf("FAIL: event loop init\n");
        event_queue_destroy(&queue);
        if (display_hal && display_hal->cleanup) {
            display_hal->cleanup();
        }
        gpio_hal->cleanup();
        return 1;
    }

    ui_thread_t ui;
    if (ui_thread_start_default(&ui, &queue, &g_loop) != 0) {
        printf("FAIL: ui thread start\n");
        event_loop_cleanup(&g_loop);
        event_queue_destroy(&queue);
        if (display_hal && display_hal->cleanup) {
            display_hal->cleanup();
        }
        gpio_hal->cleanup();
        return 1;
    }

    setup_signals();
    int rc = event_loop_run(&g_loop);
    if (rc != 0) {
        printf("FAIL: event loop run\n");
    }

    ui_thread_stop(&ui);
    event_loop_cleanup(&g_loop);
    event_queue_destroy(&queue);

    if (display_hal && display_hal->cleanup) {
        display_hal->cleanup();
    }
    gpio_hal->cleanup();

    printf("%s exit\n", APP_NAME);
    return rc == 0 ? 0 : 1;
}

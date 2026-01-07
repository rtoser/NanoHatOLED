#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

#include "event_loop.h"
#include "ui_thread.h"
#include "mocks/time_mock.h"

#ifdef __linux__

#include "hal/gpio_hal.h"

static atomic_int g_tick_count = 0;
static atomic_int g_shutdown_count = 0;

static void handler(const app_event_t *event, void *user) {
    (void)user;
    if (event->type == EVT_TICK) {
        atomic_fetch_add(&g_tick_count, 1);
    } else if (event->type == EVT_SHUTDOWN) {
        atomic_fetch_add(&g_shutdown_count, 1);
    }
}

static int stub_init(void) {
    return 0;
}

static void stub_cleanup(void) {
}

static int stub_wait_event(int timeout_ms, gpio_event_t *event) {
    (void)timeout_ms;
    (void)event;
    return 0;
}

static int stub_get_fd(void) {
    return -1;
}

static const gpio_hal_ops_t g_stub_ops = {
    .init = stub_init,
    .cleanup = stub_cleanup,
    .wait_event = stub_wait_event,
    .get_fd = stub_get_fd
};

const gpio_hal_ops_t *gpio_hal = &g_stub_ops;

static void *event_loop_thread(void *arg) {
    event_loop_t *loop = (event_loop_t *)arg;
    event_loop_run(loop);
    return NULL;
}

int main(void) {
    event_queue_t queue;
    event_loop_t loop;
    ui_thread_t ui;

    time_mock_set_now_ms(0);

    if (event_queue_init(&queue, 8) != 0) {
        fprintf(stderr, "event_queue_init failed\n");
        return 1;
    }

    if (event_loop_init(&loop, &queue) != 0) {
        fprintf(stderr, "event_loop_init failed\n");
        event_queue_destroy(&queue);
        return 1;
    }

    if (ui_thread_start(&ui, &queue, handler, NULL) != 0) {
        fprintf(stderr, "ui_thread_start failed\n");
        event_loop_cleanup(&loop);
        event_queue_destroy(&queue);
        return 1;
    }

    pthread_t th;
    pthread_create(&th, NULL, event_loop_thread, &loop);

    event_loop_request_tick(&loop, 50);

    int waited = 0;
    while (atomic_load(&g_tick_count) == 0 && waited < 200) {
        usleep(10000);
        waited++;
    }

    if (atomic_load(&g_tick_count) == 0) {
        fprintf(stderr, "tick not received\n");
        event_loop_request_shutdown(&loop);
        pthread_join(th, NULL);
        ui_thread_stop(&ui);
        event_loop_cleanup(&loop);
        event_queue_destroy(&queue);
        return 1;
    }

    event_loop_request_shutdown(&loop);
    pthread_join(th, NULL);

    waited = 0;
    while (atomic_load(&g_shutdown_count) == 0 && waited < 200) {
        usleep(10000);
        waited++;
    }

    ui_thread_stop(&ui);
    event_loop_cleanup(&loop);
    event_queue_destroy(&queue);

    if (atomic_load(&g_shutdown_count) == 0) {
        fprintf(stderr, "shutdown not received\n");
        return 1;
    }

    printf("ALL TESTS PASSED\n");
    return 0;
}

#else

int main(void) {
    printf("SKIPPED (event_loop requires Linux)\n");
    return 0;
}

#endif

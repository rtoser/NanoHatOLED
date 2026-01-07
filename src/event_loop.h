#ifndef EVENT_LOOP_H
#define EVENT_LOOP_H

#include <stdbool.h>

#include "event_queue.h"

typedef struct {
    event_queue_t *queue;
    int gpio_fd;
    int event_fd;
    int timer_fd;
    _Atomic bool running;
    _Atomic bool shutdown_requested;
    _Atomic int pending_tick_ms;
    int tick_ms;
} event_loop_t;

int event_loop_init(event_loop_t *loop, event_queue_t *queue);
int event_loop_run(event_loop_t *loop);
void event_loop_request_tick(event_loop_t *loop, int tick_ms);
void event_loop_request_shutdown(event_loop_t *loop);
void event_loop_cleanup(event_loop_t *loop);

#endif

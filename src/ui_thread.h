#ifndef UI_THREAD_H
#define UI_THREAD_H

#include <pthread.h>
#include <stdatomic.h>

#include "event_queue.h"

typedef void (*ui_event_handler_fn)(const app_event_t *event, void *user);

typedef struct {
    pthread_t thread;
    event_queue_t *queue;
    ui_event_handler_fn handler;
    void *user;
    _Atomic bool running;
} ui_thread_t;

int ui_thread_start(ui_thread_t *ui, event_queue_t *queue, ui_event_handler_fn handler, void *user);
void ui_thread_stop(ui_thread_t *ui);

#endif

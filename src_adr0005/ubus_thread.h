#ifndef UBUS_THREAD_H
#define UBUS_THREAD_H

#include <stdbool.h>
#include <pthread.h>

#include "task_queue.h"
#include "result_queue.h"

typedef struct {
    task_queue_t *task_queue;
    result_queue_t *result_queue;
    pthread_t thread;
    int wakeup_fd;
    bool running;
} ubus_thread_t;

int ubus_thread_init(ubus_thread_t *ut, task_queue_t *tq, result_queue_t *rq);
int ubus_thread_start(ubus_thread_t *ut);
void ubus_thread_stop(ubus_thread_t *ut);
void ubus_thread_destroy(ubus_thread_t *ut);

void ubus_thread_wakeup(ubus_thread_t *ut);

#endif

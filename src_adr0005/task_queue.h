#ifndef TASK_QUEUE_H
#define TASK_QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <pthread.h>

#include "ring_queue.h"
#include "hal/ubus_hal.h"

typedef enum {
    TQ_RESULT_OK = 0,
    TQ_RESULT_MERGED = 1,
    TQ_RESULT_DROPPED = 2,
    TQ_RESULT_ERR = -1
} task_queue_result_t;

typedef struct {
    uint64_t pushes;
    uint64_t pops;
    uint64_t drops;
    uint64_t merges;
    uint64_t expired;
} task_queue_stats_t;

typedef struct {
    ring_queue_t ring;
    task_queue_stats_t stats;
    pthread_mutex_t wait_lock;
    pthread_cond_t wait_cond;
    bool wait_clock_monotonic;
    _Atomic uint64_t seq;
    _Atomic bool closed;
} task_queue_t;

int task_queue_init(task_queue_t *q, size_t capacity);
void task_queue_destroy(task_queue_t *q);
void task_queue_close(task_queue_t *q);

task_queue_result_t task_queue_push(task_queue_t *q, const ubus_task_t *task);
int task_queue_try_pop(task_queue_t *q, ubus_task_t *out);
int task_queue_wait(task_queue_t *q, ubus_task_t *out, int timeout_ms);

bool task_queue_is_expired(const ubus_task_t *task, uint64_t now_ms);

task_queue_stats_t task_queue_get_stats(task_queue_t *q);

#endif

#ifndef RESULT_QUEUE_H
#define RESULT_QUEUE_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdatomic.h>
#include <pthread.h>

#include "ring_queue.h"
#include "hal/ubus_hal.h"

#define RESULT_QUEUE_MAX_ABANDONED 16

typedef enum {
    RQ_OK = 0,
    RQ_DROPPED = 1,
    RQ_ERR = -1
} result_queue_result_t;

typedef struct {
    uint64_t pushes;
    uint64_t pops;
    uint64_t drops;
    uint64_t drops_abandoned;
} result_queue_stats_t;

typedef struct {
    ring_queue_t ring;
    result_queue_stats_t stats;
    pthread_mutex_t wait_lock;
    pthread_cond_t wait_cond;
    bool wait_clock_monotonic;
    _Atomic uint64_t seq;
    _Atomic bool closed;

    uint32_t abandoned_ids[RESULT_QUEUE_MAX_ABANDONED];
    size_t abandoned_count;
    pthread_mutex_t abandoned_lock;
} result_queue_t;

int result_queue_init(result_queue_t *q, size_t capacity);
void result_queue_destroy(result_queue_t *q);
void result_queue_close(result_queue_t *q);

result_queue_result_t result_queue_push(result_queue_t *q, const ubus_result_t *result);
int result_queue_try_pop(result_queue_t *q, ubus_result_t *out);
int result_queue_wait(result_queue_t *q, ubus_result_t *out, int timeout_ms);

void result_queue_mark_abandoned(result_queue_t *q, uint32_t request_id);
void result_queue_clear_abandoned(result_queue_t *q, uint32_t request_id);

result_queue_stats_t result_queue_get_stats(result_queue_t *q);

#endif

#ifndef EVENT_QUEUE_H
#define EVENT_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <stdatomic.h>

#include "ring_queue.h"

typedef enum {
    EVT_NONE = 0,
    EVT_BTN_K1_SHORT,
    EVT_BTN_K2_SHORT,
    EVT_BTN_K3_SHORT,
    EVT_BTN_K1_LONG,
    EVT_BTN_K2_LONG,
    EVT_BTN_K3_LONG,
    EVT_TICK,
    EVT_SHUTDOWN
} app_event_type_t;

typedef struct {
    app_event_type_t type;
    uint8_t line;
    uint64_t timestamp_ns;
    uint32_t data;
} app_event_t;

typedef enum {
    EQ_RESULT_OK = 0,
    EQ_RESULT_REPLACED = 1,
    EQ_RESULT_DROPPED = 2,
    EQ_RESULT_ERR = -1
} event_queue_result_t;

typedef struct {
    ring_queue_t ring;
    uint64_t replaced_ticks;
    uint64_t dropped_critical;
    pthread_mutex_t wait_lock;
    pthread_cond_t wait_cond;
    bool wait_clock_monotonic;
    _Atomic uint64_t seq;
    _Atomic bool closed;
} event_queue_t;

int event_queue_init(event_queue_t *q, size_t capacity);
void event_queue_destroy(event_queue_t *q);
void event_queue_close(event_queue_t *q);

event_queue_result_t event_queue_push(event_queue_t *q, const app_event_t *event);
int event_queue_try_pop(event_queue_t *q, app_event_t *out);
int event_queue_wait(event_queue_t *q, app_event_t *out, int timeout_ms);

#endif

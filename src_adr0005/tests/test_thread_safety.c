#include <stdio.h>
#include <pthread.h>
#include <stdatomic.h>

#include "event_queue.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define PRODUCERS 4
#define EVENTS_PER_PRODUCER 200
#define TOTAL_EVENTS (PRODUCERS * EVENTS_PER_PRODUCER)

typedef struct {
    event_queue_t *queue;
    int producer_id;
} producer_ctx_t;

static void *producer_thread(void *arg) {
    producer_ctx_t *ctx = (producer_ctx_t *)arg;
    for (int i = 0; i < EVENTS_PER_PRODUCER; i++) {
        app_event_t evt = {
            .type = EVT_BTN_K1_SHORT,
            .line = (uint8_t)ctx->producer_id,
            .timestamp_ns = (uint64_t)i,
            .data = (uint32_t)i
        };
        event_queue_push(ctx->queue, &evt);
    }
    return NULL;
}

typedef struct {
    event_queue_t *queue;
    atomic_int *consumed;
} consumer_ctx_t;

static void *consumer_thread(void *arg) {
    consumer_ctx_t *ctx = (consumer_ctx_t *)arg;
    int local = 0;
    while (local < TOTAL_EVENTS) {
        app_event_t evt = {0};
        int ret = event_queue_wait(ctx->queue, &evt, -1);
        if (ret == 1) {
            local++;
        }
    }
    atomic_fetch_add(ctx->consumed, local);
    return NULL;
}

int main(void) {
    event_queue_t q;
    TEST_ASSERT(event_queue_init(&q, 1024) == 0);

    pthread_t producers[PRODUCERS];
    producer_ctx_t producer_ctx[PRODUCERS];

    atomic_int consumed = 0;
    consumer_ctx_t consumer_ctx = {.queue = &q, .consumed = &consumed};
    pthread_t consumer;

    TEST_ASSERT(pthread_create(&consumer, NULL, consumer_thread, &consumer_ctx) == 0);

    for (int i = 0; i < PRODUCERS; i++) {
        producer_ctx[i].queue = &q;
        producer_ctx[i].producer_id = i;
        TEST_ASSERT(pthread_create(&producers[i], NULL, producer_thread, &producer_ctx[i]) == 0);
    }

    for (int i = 0; i < PRODUCERS; i++) {
        pthread_join(producers[i], NULL);
    }

    pthread_join(consumer, NULL);

    TEST_ASSERT(atomic_load(&consumed) == TOTAL_EVENTS);

    event_queue_destroy(&q);
    printf("ALL TESTS PASSED\n");
    return 0;
}

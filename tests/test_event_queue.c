#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

#include "event_queue.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

static app_event_t make_tick(uint64_t ts) {
    app_event_t evt = {
        .type = EVT_TICK,
        .line = 0,
        .timestamp_ns = ts,
        .data = 0
    };
    return evt;
}

static app_event_t make_btn(app_event_type_t type, uint8_t line) {
    app_event_t evt = {
        .type = type,
        .line = line,
        .timestamp_ns = 0,
        .data = 0
    };
    return evt;
}

static int test_tick_merge(void) {
    event_queue_t q;
    TEST_ASSERT(event_queue_init(&q, 4) == 0);

    app_event_t t1 = make_tick(100);
    app_event_t t2 = make_tick(200);

    TEST_ASSERT(event_queue_push(&q, &t1) == EQ_RESULT_OK);
    TEST_ASSERT(event_queue_push(&q, &t2) == EQ_RESULT_OK);

    app_event_t out = {0};
    TEST_ASSERT(event_queue_try_pop(&q, &out) == 1);
    TEST_ASSERT(out.type == EVT_TICK);
    TEST_ASSERT(out.timestamp_ns == 200);

    event_queue_destroy(&q);
    return 0;
}

static int test_critical_replaces_tick(void) {
    event_queue_t q;
    TEST_ASSERT(event_queue_init(&q, 2) == 0);

    app_event_t t1 = make_tick(100);
    app_event_t t2 = make_tick(200);
    TEST_ASSERT(event_queue_push(&q, &t1) == EQ_RESULT_OK);
    TEST_ASSERT(event_queue_push(&q, &t2) == EQ_RESULT_OK);

    app_event_t k1 = make_btn(EVT_BTN_K1_SHORT, 0);
    event_queue_result_t res = event_queue_push(&q, &k1);
    TEST_ASSERT(res == EQ_RESULT_REPLACED || res == EQ_RESULT_OK);

    app_event_t out1 = {0};
    app_event_t out2 = {0};
    TEST_ASSERT(event_queue_try_pop(&q, &out1) == 1);
    TEST_ASSERT(event_queue_try_pop(&q, &out2) == 1);

    int critical_seen = 0;
    if (out1.type == EVT_BTN_K1_SHORT) {
        critical_seen++;
    }
    if (out2.type == EVT_BTN_K1_SHORT) {
        critical_seen++;
    }
    TEST_ASSERT(critical_seen == 1);

    event_queue_destroy(&q);
    return 0;
}

static int test_wait_timeout(void) {
    event_queue_t q;
    TEST_ASSERT(event_queue_init(&q, 4) == 0);

    app_event_t out = {0};
    int ret = event_queue_wait(&q, &out, 50);
    TEST_ASSERT(ret == 0);

    event_queue_destroy(&q);
    return 0;
}

typedef struct {
    event_queue_t *q;
} waiter_ctx_t;

static void *waiter_thread(void *arg) {
    waiter_ctx_t *ctx = (waiter_ctx_t *)arg;
    app_event_t out = {0};
    event_queue_wait(ctx->q, &out, -1);
    return NULL;
}

static int test_close_wakes_waiter(void) {
    event_queue_t q;
    TEST_ASSERT(event_queue_init(&q, 4) == 0);

    pthread_t th;
    waiter_ctx_t ctx = {.q = &q};
    TEST_ASSERT(pthread_create(&th, NULL, waiter_thread, &ctx) == 0);

    usleep(10000);
    event_queue_close(&q);
    pthread_join(th, NULL);

    event_queue_destroy(&q);
    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_tick_merge();
    rc |= test_critical_replaces_tick();
    rc |= test_wait_timeout();
    rc |= test_close_wakes_waiter();

    if (rc == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return rc;
}

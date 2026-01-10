#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <stdatomic.h>
#include <sched.h>

#include "ring_queue.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

static int test_queue_init_empty(void) {
    ring_queue_t q;
    TEST_ASSERT(ring_queue_init(&q, 4, sizeof(int)) == 0);
    TEST_ASSERT(ring_queue_count(&q) == 0);
    ring_queue_destroy(&q);
    return 0;
}

static int test_queue_push_pop(void) {
    ring_queue_t q;
    int out = -1;
    TEST_ASSERT(ring_queue_init(&q, 4, sizeof(int)) == 0);
    int v = 42;
    TEST_ASSERT(ring_queue_push(&q, &v) == RQ_RESULT_OK);
    TEST_ASSERT(ring_queue_pop(&q, &out) == true);
    TEST_ASSERT(out == 42);
    TEST_ASSERT(ring_queue_count(&q) == 0);
    ring_queue_destroy(&q);
    return 0;
}

static int test_queue_overwrite_policy(void) {
    ring_queue_t q;
    TEST_ASSERT(ring_queue_init(&q, 4, sizeof(int)) == 0);
    ring_queue_set_overflow_policy(&q, RQ_OVERWRITE_OLDEST);

    for (int i = 0; i < 6; i++) {
        TEST_ASSERT(ring_queue_push(&q, &i) == RQ_RESULT_OK);
    }

    int out = -1;
    TEST_ASSERT(ring_queue_pop(&q, &out) == true);
    TEST_ASSERT(out == 2);
    TEST_ASSERT(ring_queue_pop(&q, &out) == true);
    TEST_ASSERT(out == 3);
    TEST_ASSERT(ring_queue_pop(&q, &out) == true);
    TEST_ASSERT(out == 4);
    TEST_ASSERT(ring_queue_pop(&q, &out) == true);
    TEST_ASSERT(out == 5);
    TEST_ASSERT(ring_queue_pop(&q, &out) == false);

    ring_queue_destroy(&q);
    return 0;
}

static int test_queue_reject_policy(void) {
    ring_queue_t q;
    TEST_ASSERT(ring_queue_init(&q, 4, sizeof(int)) == 0);
    ring_queue_set_overflow_policy(&q, RQ_REJECT_NEW);

    for (int i = 0; i < 4; i++) {
        TEST_ASSERT(ring_queue_push(&q, &i) == RQ_RESULT_OK);
    }

    int v = 99;
    TEST_ASSERT(ring_queue_push(&q, &v) == RQ_RESULT_DROPPED);

    int out = -1;
    for (int i = 0; i < 4; i++) {
        TEST_ASSERT(ring_queue_pop(&q, &out) == true);
        TEST_ASSERT(out == i);
    }
    TEST_ASSERT(ring_queue_pop(&q, &out) == false);

    ring_queue_destroy(&q);
    return 0;
}

typedef struct {
    int key;
    int value;
} kv_item_t;

static bool merge_sum(void *existing, const void *incoming, void *user) {
    (void)user;
    kv_item_t *a = (kv_item_t *)existing;
    const kv_item_t *b = (const kv_item_t *)incoming;
    if (a->key != b->key) {
        return false;
    }
    a->value += b->value;
    return true;
}

static int test_queue_coalesce_policy(void) {
    ring_queue_t q;
    TEST_ASSERT(ring_queue_init(&q, 2, sizeof(kv_item_t)) == 0);
    ring_queue_set_overflow_policy(&q, RQ_COALESCE);
    ring_queue_set_merge_fn(&q, merge_sum, NULL);

    kv_item_t a = {.key = 1, .value = 1};
    kv_item_t b = {.key = 1, .value = 2};
    TEST_ASSERT(ring_queue_push(&q, &a) == RQ_RESULT_OK);
    TEST_ASSERT(ring_queue_push(&q, &b) == RQ_RESULT_MERGED);
    TEST_ASSERT(ring_queue_count(&q) == 1);

    kv_item_t out = {0};
    TEST_ASSERT(ring_queue_pop(&q, &out) == true);
    TEST_ASSERT(out.key == 1);
    TEST_ASSERT(out.value == 3);

    kv_item_t c = {.key = 2, .value = 5};
    kv_item_t d = {.key = 3, .value = 7};
    kv_item_t e = {.key = 4, .value = 9};
    TEST_ASSERT(ring_queue_push(&q, &c) == RQ_RESULT_OK);
    TEST_ASSERT(ring_queue_push(&q, &d) == RQ_RESULT_OK);
    TEST_ASSERT(ring_queue_push(&q, &e) == RQ_RESULT_DROPPED);

    ring_queue_destroy(&q);
    return 0;
}

typedef struct {
    ring_queue_t *q;
    int start;
    int count;
    atomic_int *active;
} producer_arg_t;

typedef struct {
    ring_queue_t *q;
    atomic_int *active;
} consumer_arg_t;

static void *producer_thread(void *arg) {
    producer_arg_t *pa = (producer_arg_t *)arg;
    for (int i = 0; i < pa->count; i++) {
        int v = pa->start + i;
        ring_queue_push(pa->q, &v);
    }
    atomic_fetch_sub(pa->active, 1);
    return NULL;
}

static void *consumer_thread(void *arg) {
    consumer_arg_t *ca = (consumer_arg_t *)arg;
    ring_queue_t *q = ca->q;
    int out = 0;
    while (atomic_load(ca->active) > 0 || ring_queue_count(q) > 0) {
        if (ring_queue_pop(q, &out)) {
            continue;
        }
        sched_yield();
    }
    return NULL;
}

static int test_queue_thread_safety(void) {
    ring_queue_t q;
    TEST_ASSERT(ring_queue_init(&q, 128, sizeof(int)) == 0);
    ring_queue_set_overflow_policy(&q, RQ_OVERWRITE_OLDEST);

    atomic_int active = 2;
    pthread_t t1;
    pthread_t t2;
    pthread_t tc;

    producer_arg_t a1 = {.q = &q, .start = 0, .count = 10000, .active = &active};
    producer_arg_t a2 = {.q = &q, .start = 10000, .count = 10000, .active = &active};
    consumer_arg_t ca = {.q = &q, .active = &active};

    pthread_create(&t1, NULL, producer_thread, &a1);
    pthread_create(&t2, NULL, producer_thread, &a2);
    pthread_create(&tc, NULL, consumer_thread, &ca);

    pthread_join(t1, NULL);
    pthread_join(t2, NULL);

    pthread_join(tc, NULL);

    ring_queue_stats_t stats = ring_queue_get_stats(&q);
    TEST_ASSERT(stats.pushes == 20000);
    TEST_ASSERT(ring_queue_count(&q) == 0);

    ring_queue_destroy(&q);
    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_queue_init_empty();
    rc |= test_queue_push_pop();
    rc |= test_queue_overwrite_policy();
    rc |= test_queue_reject_policy();
    rc |= test_queue_coalesce_policy();
    rc |= test_queue_thread_safety();

    if (rc == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return rc;
}

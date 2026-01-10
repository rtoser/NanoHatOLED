#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "task_queue.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

static ubus_task_t make_task(const char *service, ubus_action_t action, uint32_t req_id) {
    ubus_task_t task = {0};
    strncpy(task.service_name, service, sizeof(task.service_name) - 1);
    task.action = action;
    task.request_id = req_id;
    task.timeout_ms = 5000;
    task.enqueue_time_ms = 1000;
    return task;
}

static int test_push_pop(void) {
    task_queue_t q;
    TEST_ASSERT(task_queue_init(&q, 4) == 0);

    ubus_task_t t1 = make_task("network", UBUS_ACTION_QUERY, 1);
    TEST_ASSERT(task_queue_push(&q, &t1) == TQ_RESULT_OK);

    ubus_task_t out = {0};
    TEST_ASSERT(task_queue_try_pop(&q, &out) == 1);
    TEST_ASSERT(strcmp(out.service_name, "network") == 0);
    TEST_ASSERT(out.request_id == 1);

    task_queue_destroy(&q);
    return 0;
}

static int test_merge_same_service_action(void) {
    task_queue_t q;
    TEST_ASSERT(task_queue_init(&q, 4) == 0);

    ubus_task_t t1 = make_task("system", UBUS_ACTION_QUERY, 1);
    ubus_task_t t2 = make_task("system", UBUS_ACTION_QUERY, 2);

    TEST_ASSERT(task_queue_push(&q, &t1) == TQ_RESULT_OK);
    TEST_ASSERT(task_queue_push(&q, &t2) == TQ_RESULT_MERGED);

    ubus_task_t out = {0};
    TEST_ASSERT(task_queue_try_pop(&q, &out) == 1);
    TEST_ASSERT(out.request_id == 2);
    TEST_ASSERT(task_queue_try_pop(&q, &out) == 0);

    task_queue_destroy(&q);
    return 0;
}

static int test_no_merge_different_action(void) {
    task_queue_t q;
    TEST_ASSERT(task_queue_init(&q, 4) == 0);

    ubus_task_t t1 = make_task("system", UBUS_ACTION_START, 1);
    ubus_task_t t2 = make_task("system", UBUS_ACTION_STOP, 2);

    TEST_ASSERT(task_queue_push(&q, &t1) == TQ_RESULT_OK);
    TEST_ASSERT(task_queue_push(&q, &t2) == TQ_RESULT_OK);

    ubus_task_t out = {0};
    TEST_ASSERT(task_queue_try_pop(&q, &out) == 1);
    TEST_ASSERT(out.request_id == 1);
    TEST_ASSERT(task_queue_try_pop(&q, &out) == 1);
    TEST_ASSERT(out.request_id == 2);

    task_queue_destroy(&q);
    return 0;
}

static int test_drop_when_full(void) {
    task_queue_t q;
    TEST_ASSERT(task_queue_init(&q, 2) == 0);

    ubus_task_t t1 = make_task("s1", UBUS_ACTION_START, 1);
    ubus_task_t t2 = make_task("s2", UBUS_ACTION_START, 2);
    ubus_task_t t3 = make_task("s3", UBUS_ACTION_START, 3);

    TEST_ASSERT(task_queue_push(&q, &t1) == TQ_RESULT_OK);
    TEST_ASSERT(task_queue_push(&q, &t2) == TQ_RESULT_OK);
    TEST_ASSERT(task_queue_push(&q, &t3) == TQ_RESULT_DROPPED);

    task_queue_stats_t stats = task_queue_get_stats(&q);
    TEST_ASSERT(stats.drops == 1);

    task_queue_destroy(&q);
    return 0;
}

static int test_is_expired(void) {
    ubus_task_t task = make_task("test", UBUS_ACTION_QUERY, 1);
    task.enqueue_time_ms = 1000;
    task.timeout_ms = 500;

    TEST_ASSERT(task_queue_is_expired(&task, 1400) == false);
    TEST_ASSERT(task_queue_is_expired(&task, 1501) == true);

    task.timeout_ms = 0;
    TEST_ASSERT(task_queue_is_expired(&task, 9999) == false);

    return 0;
}

static int test_wait_timeout(void) {
    task_queue_t q;
    TEST_ASSERT(task_queue_init(&q, 4) == 0);

    ubus_task_t out = {0};
    int ret = task_queue_wait(&q, &out, 50);
    TEST_ASSERT(ret == 0);

    task_queue_destroy(&q);
    return 0;
}

typedef struct {
    task_queue_t *q;
} waiter_ctx_t;

static void *waiter_thread(void *arg) {
    waiter_ctx_t *ctx = (waiter_ctx_t *)arg;
    ubus_task_t out = {0};
    task_queue_wait(ctx->q, &out, -1);
    return NULL;
}

static int test_close_wakes_waiter(void) {
    task_queue_t q;
    TEST_ASSERT(task_queue_init(&q, 4) == 0);

    pthread_t th;
    waiter_ctx_t ctx = {.q = &q};
    TEST_ASSERT(pthread_create(&th, NULL, waiter_thread, &ctx) == 0);

    usleep(10000);
    task_queue_close(&q);
    pthread_join(th, NULL);

    task_queue_destroy(&q);
    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_push_pop();
    rc |= test_merge_same_service_action();
    rc |= test_no_merge_different_action();
    rc |= test_drop_when_full();
    rc |= test_is_expired();
    rc |= test_wait_timeout();
    rc |= test_close_wakes_waiter();

    if (rc == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return rc;
}

#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "result_queue.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

static ubus_result_t make_result(const char *service, ubus_action_t action, uint32_t req_id, bool success) {
    ubus_result_t result = {0};
    strncpy(result.service_name, service, sizeof(result.service_name) - 1);
    result.action = action;
    result.request_id = req_id;
    result.success = success;
    result.error_code = success ? 0 : -1;
    return result;
}

static int test_push_pop(void) {
    result_queue_t q;
    TEST_ASSERT(result_queue_init(&q, 4) == 0);

    ubus_result_t r1 = make_result("network", UBUS_ACTION_QUERY, 1, true);
    TEST_ASSERT(result_queue_push(&q, &r1) == RQ_OK);

    ubus_result_t out = {0};
    TEST_ASSERT(result_queue_try_pop(&q, &out) == 1);
    TEST_ASSERT(strcmp(out.service_name, "network") == 0);
    TEST_ASSERT(out.request_id == 1);
    TEST_ASSERT(out.success == true);

    result_queue_destroy(&q);
    return 0;
}

static int test_drop_when_full(void) {
    result_queue_t q;
    TEST_ASSERT(result_queue_init(&q, 2) == 0);

    ubus_result_t r1 = make_result("s1", UBUS_ACTION_QUERY, 1, true);
    ubus_result_t r2 = make_result("s2", UBUS_ACTION_QUERY, 2, true);
    ubus_result_t r3 = make_result("s3", UBUS_ACTION_QUERY, 3, true);

    TEST_ASSERT(result_queue_push(&q, &r1) == RQ_OK);
    TEST_ASSERT(result_queue_push(&q, &r2) == RQ_OK);
    TEST_ASSERT(result_queue_push(&q, &r3) == RQ_DROPPED);

    result_queue_stats_t stats = result_queue_get_stats(&q);
    TEST_ASSERT(stats.drops == 1);

    result_queue_destroy(&q);
    return 0;
}

static int test_mark_abandoned_drops(void) {
    result_queue_t q;
    TEST_ASSERT(result_queue_init(&q, 4) == 0);

    result_queue_mark_abandoned(&q, 100);

    ubus_result_t r1 = make_result("test", UBUS_ACTION_QUERY, 100, true);
    TEST_ASSERT(result_queue_push(&q, &r1) == RQ_DROPPED);

    result_queue_stats_t stats = result_queue_get_stats(&q);
    TEST_ASSERT(stats.drops_abandoned == 1);

    result_queue_destroy(&q);
    return 0;
}

static int test_replace_abandoned_when_full(void) {
    result_queue_t q;
    TEST_ASSERT(result_queue_init(&q, 2) == 0);

    ubus_result_t r1 = make_result("s1", UBUS_ACTION_QUERY, 1, true);
    ubus_result_t r2 = make_result("s2", UBUS_ACTION_QUERY, 2, true);
    TEST_ASSERT(result_queue_push(&q, &r1) == RQ_OK);
    TEST_ASSERT(result_queue_push(&q, &r2) == RQ_OK);

    result_queue_mark_abandoned(&q, 1);

    ubus_result_t r3 = make_result("s3", UBUS_ACTION_QUERY, 3, true);
    TEST_ASSERT(result_queue_push(&q, &r3) == RQ_OK);

    ubus_result_t out = {0};
    TEST_ASSERT(result_queue_try_pop(&q, &out) == 1);
    TEST_ASSERT(out.request_id == 3);
    TEST_ASSERT(result_queue_try_pop(&q, &out) == 1);
    TEST_ASSERT(out.request_id == 2);

    result_queue_destroy(&q);
    return 0;
}

static int test_clear_abandoned(void) {
    result_queue_t q;
    TEST_ASSERT(result_queue_init(&q, 4) == 0);

    result_queue_mark_abandoned(&q, 100);
    result_queue_clear_abandoned(&q, 100);

    ubus_result_t r1 = make_result("test", UBUS_ACTION_QUERY, 100, true);
    TEST_ASSERT(result_queue_push(&q, &r1) == RQ_OK);

    result_queue_destroy(&q);
    return 0;
}

static int test_wait_timeout(void) {
    result_queue_t q;
    TEST_ASSERT(result_queue_init(&q, 4) == 0);

    ubus_result_t out = {0};
    int ret = result_queue_wait(&q, &out, 50);
    TEST_ASSERT(ret == 0);

    result_queue_destroy(&q);
    return 0;
}

typedef struct {
    result_queue_t *q;
} waiter_ctx_t;

static void *waiter_thread(void *arg) {
    waiter_ctx_t *ctx = (waiter_ctx_t *)arg;
    ubus_result_t out = {0};
    result_queue_wait(ctx->q, &out, -1);
    return NULL;
}

static int test_close_wakes_waiter(void) {
    result_queue_t q;
    TEST_ASSERT(result_queue_init(&q, 4) == 0);

    pthread_t th;
    waiter_ctx_t ctx = {.q = &q};
    TEST_ASSERT(pthread_create(&th, NULL, waiter_thread, &ctx) == 0);

    usleep(10000);
    result_queue_close(&q);
    pthread_join(th, NULL);

    result_queue_destroy(&q);
    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_push_pop();
    rc |= test_drop_when_full();
    rc |= test_mark_abandoned_drops();
    rc |= test_replace_abandoned_when_full();
    rc |= test_clear_abandoned();
    rc |= test_wait_timeout();
    rc |= test_close_wakes_waiter();

    if (rc == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return rc;
}

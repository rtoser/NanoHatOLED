#include <stdio.h>
#include <string.h>
#include <pthread.h>
#include <unistd.h>

#include "task_queue.h"
#include "result_queue.h"
#include "ubus_thread.h"
#include "hal/ubus_hal.h"
#include "hal/time_hal.h"
#include "ubus_mock.h"

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
    task.enqueue_time_ms = time_hal_now_ms();
    return task;
}

static int test_basic_invoke(void) {
    task_queue_t tq;
    result_queue_t rq;
    ubus_thread_t ut;

    TEST_ASSERT(task_queue_init(&tq, 8) == 0);
    TEST_ASSERT(result_queue_init(&rq, 8) == 0);
    TEST_ASSERT(ubus_thread_init(&ut, &tq, &rq) == 0);

    ubus_hal_mock_reset();
    TEST_ASSERT(ubus_thread_start(&ut) == 0);

    ubus_task_t task = make_task("network", UBUS_ACTION_QUERY, 1);
    TEST_ASSERT(task_queue_push(&tq, &task) == TQ_RESULT_OK);
    ubus_thread_wakeup(&ut);

    ubus_result_t result = {0};
    int ret = result_queue_wait(&rq, &result, 1000);
    TEST_ASSERT(ret == 1);
    TEST_ASSERT(strcmp(result.service_name, "network") == 0);
    TEST_ASSERT(result.request_id == 1);
    TEST_ASSERT(result.success == true);

    ubus_thread_stop(&ut);
    ubus_thread_destroy(&ut);
    result_queue_destroy(&rq);
    task_queue_destroy(&tq);
    return 0;
}

static int test_expired_task(void) {
    task_queue_t tq;
    result_queue_t rq;
    ubus_thread_t ut;

    TEST_ASSERT(task_queue_init(&tq, 8) == 0);
    TEST_ASSERT(result_queue_init(&rq, 8) == 0);
    TEST_ASSERT(ubus_thread_init(&ut, &tq, &rq) == 0);

    ubus_hal_mock_reset();
    ubus_hal_mock_set_delay(100);
    TEST_ASSERT(ubus_thread_start(&ut) == 0);

    ubus_task_t task = make_task("system", UBUS_ACTION_QUERY, 2);
    task.timeout_ms = 10;
    task.enqueue_time_ms = time_hal_now_ms() - 100;

    TEST_ASSERT(task_queue_push(&tq, &task) == TQ_RESULT_OK);
    ubus_thread_wakeup(&ut);

    ubus_result_t result = {0};
    int ret = result_queue_wait(&rq, &result, 1000);
    TEST_ASSERT(ret == 1);
    TEST_ASSERT(result.request_id == 2);
    TEST_ASSERT(result.success == false);

    ubus_thread_stop(&ut);
    ubus_thread_destroy(&ut);
    result_queue_destroy(&rq);
    task_queue_destroy(&tq);
    return 0;
}

static int test_multiple_tasks(void) {
    task_queue_t tq;
    result_queue_t rq;
    ubus_thread_t ut;

    TEST_ASSERT(task_queue_init(&tq, 8) == 0);
    TEST_ASSERT(result_queue_init(&rq, 8) == 0);
    TEST_ASSERT(ubus_thread_init(&ut, &tq, &rq) == 0);

    ubus_hal_mock_reset();
    TEST_ASSERT(ubus_thread_start(&ut) == 0);

    char svc_name[32];
    for (int i = 0; i < 5; i++) {
        snprintf(svc_name, sizeof(svc_name), "svc%d", i);
        ubus_task_t task = make_task(svc_name, UBUS_ACTION_QUERY, (uint32_t)(10 + i));
        task_queue_result_t res = task_queue_push(&tq, &task);
        TEST_ASSERT(res == TQ_RESULT_OK || res == TQ_RESULT_MERGED);
    }
    ubus_thread_wakeup(&ut);

    int received = 0;
    for (int i = 0; i < 5; i++) {
        ubus_result_t result = {0};
        int ret = result_queue_wait(&rq, &result, 200);
        if (ret == 1) {
            TEST_ASSERT(result.success == true);
            received++;
        }
    }

    TEST_ASSERT(received >= 1);
    TEST_ASSERT(ubus_hal_mock_get_call_count() >= 1);

    ubus_thread_stop(&ut);
    ubus_thread_destroy(&ut);
    result_queue_destroy(&rq);
    task_queue_destroy(&tq);
    return 0;
}

static int test_graceful_shutdown(void) {
    task_queue_t tq;
    result_queue_t rq;
    ubus_thread_t ut;

    TEST_ASSERT(task_queue_init(&tq, 8) == 0);
    TEST_ASSERT(result_queue_init(&rq, 8) == 0);
    TEST_ASSERT(ubus_thread_init(&ut, &tq, &rq) == 0);

    ubus_hal_mock_reset();
    TEST_ASSERT(ubus_thread_start(&ut) == 0);

    ubus_task_t task = make_task("pending", UBUS_ACTION_QUERY, 100);
    TEST_ASSERT(task_queue_push(&tq, &task) == TQ_RESULT_OK);

    ubus_thread_stop(&ut);

    ubus_result_t result = {0};
    int ret = result_queue_try_pop(&rq, &result);
    TEST_ASSERT(ret == 1);
    TEST_ASSERT(result.request_id == 100);

    ubus_thread_destroy(&ut);
    result_queue_destroy(&rq);
    task_queue_destroy(&tq);
    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_basic_invoke();
    rc |= test_expired_task();
    rc |= test_multiple_tasks();
    rc |= test_graceful_shutdown();

    if (rc == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return rc;
}

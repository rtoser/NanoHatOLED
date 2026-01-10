#include "hal/ubus_hal.h"

#include <string.h>
#include <unistd.h>

static int mock_delay_ms = 0;
static int mock_fail_count = 0;
static int mock_call_count = 0;

void ubus_hal_mock_set_delay(int delay_ms) {
    mock_delay_ms = delay_ms;
}

void ubus_hal_mock_set_fail_count(int count) {
    mock_fail_count = count;
}

void ubus_hal_mock_reset(void) {
    mock_delay_ms = 0;
    mock_fail_count = 0;
    mock_call_count = 0;
}

int ubus_hal_mock_get_call_count(void) {
    return mock_call_count;
}

static int mock_init(void) {
    mock_call_count = 0;
    return 0;
}

static void mock_cleanup(void) {
    mock_call_count = 0;
}

static int mock_invoke(const ubus_task_t *task, ubus_result_t *result) {
    mock_call_count++;

    if (mock_delay_ms > 0) {
        usleep((useconds_t)mock_delay_ms * 1000);
    }

    memset(result, 0, sizeof(*result));
    strncpy(result->service_name, task->service_name, sizeof(result->service_name) - 1);
    result->action = task->action;
    result->request_id = task->request_id;

    if (mock_fail_count > 0) {
        mock_fail_count--;
        result->success = false;
        result->error_code = -1;
        return -1;
    }

    result->success = true;
    result->error_code = 0;

    /* For QUERY action, simulate service status */
    if (task->action == UBUS_ACTION_QUERY) {
        result->installed = true;
        result->running = true;
    }

    return 0;
}

static int mock_register_object(void) {
    return 0;
}

static void mock_unregister_object(void) {
}

static const ubus_hal_ops_t mock_ops = {
    .init = mock_init,
    .cleanup = mock_cleanup,
    .invoke = mock_invoke,
    .register_object = mock_register_object,
    .unregister_object = mock_unregister_object
};

const ubus_hal_ops_t *ubus_hal = &mock_ops;

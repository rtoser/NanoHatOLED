#define _POSIX_C_SOURCE 200809L

#include "ubus_thread.h"
#include "hal/ubus_hal.h"
#include "hal/time_hal.h"

#include <errno.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>

#ifdef __linux__
#include <sys/eventfd.h>
#else
/* Test host compatibility (e.g., macOS) - production runs on Linux only */
#include "eventfd_compat.h"
#endif

static void process_task(ubus_thread_t *ut, ubus_task_t *task) {
    uint64_t now_ms = time_hal_now_ms();

    if (task_queue_is_expired(task, now_ms)) {
        ubus_result_t result = {0};
        strncpy(result.service_name, task->service_name, sizeof(result.service_name) - 1);
        result.action = task->action;
        result.request_id = task->request_id;
        result.success = false;
        result.error_code = -ETIMEDOUT;
        result_queue_push(ut->result_queue, &result);
        return;
    }

    ubus_result_t result = {0};
    if (ubus_hal && ubus_hal->invoke) {
        ubus_hal->invoke(task, &result);
    } else {
        strncpy(result.service_name, task->service_name, sizeof(result.service_name) - 1);
        result.action = task->action;
        result.request_id = task->request_id;
        result.success = false;
        result.error_code = -ENOTSUP;
    }

    result_queue_push(ut->result_queue, &result);
}

static void *ubus_thread_main(void *arg) {
    ubus_thread_t *ut = (ubus_thread_t *)arg;

    struct pollfd pfd = {
        .fd = ut->wakeup_fd,
        .events = POLLIN,
        .revents = 0
    };

    while (ut->running) {
        ubus_task_t task;
        while (task_queue_try_pop(ut->task_queue, &task) == 1) {
            process_task(ut, &task);
        }

        int ret = poll(&pfd, 1, 100);
        if (ret > 0 && (pfd.revents & POLLIN)) {
            uint64_t val;
            while (read(ut->wakeup_fd, &val, sizeof(val)) > 0) {}
        }
    }

    ubus_task_t task;
    while (task_queue_try_pop(ut->task_queue, &task) == 1) {
        ubus_result_t result = {0};
        strncpy(result.service_name, task.service_name, sizeof(result.service_name) - 1);
        result.action = task.action;
        result.request_id = task.request_id;
        result.success = false;
        result.error_code = -ECANCELED;
        result_queue_push(ut->result_queue, &result);
    }

    return NULL;
}

int ubus_thread_init(ubus_thread_t *ut, task_queue_t *tq, result_queue_t *rq) {
    if (!ut || !tq || !rq) {
        return -1;
    }

    memset(ut, 0, sizeof(*ut));
    ut->task_queue = tq;
    ut->result_queue = rq;
    ut->wakeup_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (ut->wakeup_fd < 0) {
        return -1;
    }
    ut->running = false;
    return 0;
}

int ubus_thread_start(ubus_thread_t *ut) {
    if (!ut || ut->running) {
        return -1;
    }

    ut->running = true;
    if (pthread_create(&ut->thread, NULL, ubus_thread_main, ut) != 0) {
        ut->running = false;
        return -1;
    }
    return 0;
}

void ubus_thread_stop(ubus_thread_t *ut) {
    if (!ut || !ut->running) {
        return;
    }

    ut->running = false;
    ubus_thread_wakeup(ut);
    pthread_join(ut->thread, NULL);
}

void ubus_thread_destroy(ubus_thread_t *ut) {
    if (!ut) {
        return;
    }
    if (ut->running) {
        ubus_thread_stop(ut);
    }
    if (ut->wakeup_fd >= 0) {
#ifdef __linux__
        close(ut->wakeup_fd);
#else
        eventfd_compat_close(ut->wakeup_fd);
#endif
        ut->wakeup_fd = -1;
    }
}

void ubus_thread_wakeup(ubus_thread_t *ut) {
    if (!ut || ut->wakeup_fd < 0) {
        return;
    }
#ifdef __linux__
    uint64_t val = 1;
    ssize_t ret = write(ut->wakeup_fd, &val, sizeof(val));
    (void)ret;
#else
    eventfd_compat_wakeup(ut->wakeup_fd);
#endif
}

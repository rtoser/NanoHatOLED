#define _POSIX_C_SOURCE 200809L

#include "task_queue.h"

#include <errno.h>
#include <string.h>
#include <time.h>

static bool task_queue_merge_same_service_action(void *existing, const void *incoming, void *user) {
    (void)user;
    ubus_task_t *ex = (ubus_task_t *)existing;
    const ubus_task_t *in = (const ubus_task_t *)incoming;

    if (ex->action == in->action &&
        strncmp(ex->service_name, in->service_name, sizeof(ex->service_name)) == 0) {
        ex->request_id = in->request_id;
        ex->timeout_ms = in->timeout_ms;
        ex->enqueue_time_ms = in->enqueue_time_ms;
        return true;
    }
    return false;
}

static void task_queue_signal(task_queue_t *q) {
    atomic_fetch_add(&q->seq, 1);
    pthread_mutex_lock(&q->wait_lock);
    pthread_cond_signal(&q->wait_cond);
    pthread_mutex_unlock(&q->wait_lock);
}

int task_queue_init(task_queue_t *q, size_t capacity) {
    if (!q || capacity == 0) {
        return -1;
    }

    memset(q, 0, sizeof(*q));
    if (ring_queue_init(&q->ring, capacity, sizeof(ubus_task_t)) != 0) {
        return -1;
    }
    ring_queue_set_overflow_policy(&q->ring, RQ_COALESCE);
    ring_queue_set_merge_fn(&q->ring, task_queue_merge_same_service_action, NULL);

    pthread_mutex_init(&q->wait_lock, NULL);
    q->wait_clock_monotonic = false;
#ifdef __linux__
    pthread_condattr_t attr;
    if (pthread_condattr_init(&attr) == 0) {
        if (pthread_condattr_setclock(&attr, CLOCK_MONOTONIC) == 0) {
            pthread_cond_init(&q->wait_cond, &attr);
            q->wait_clock_monotonic = true;
        } else {
            pthread_cond_init(&q->wait_cond, NULL);
        }
        pthread_condattr_destroy(&attr);
    } else {
        pthread_cond_init(&q->wait_cond, NULL);
    }
#else
    pthread_cond_init(&q->wait_cond, NULL);
#endif
    atomic_store(&q->seq, 0);
    atomic_store(&q->closed, false);
    return 0;
}

void task_queue_destroy(task_queue_t *q) {
    if (!q) {
        return;
    }
    ring_queue_destroy(&q->ring);
    pthread_mutex_destroy(&q->wait_lock);
    pthread_cond_destroy(&q->wait_cond);
    memset(q, 0, sizeof(*q));
}

void task_queue_close(task_queue_t *q) {
    if (!q) {
        return;
    }
    atomic_store(&q->closed, true);
    task_queue_signal(q);
}

task_queue_result_t task_queue_push(task_queue_t *q, const ubus_task_t *task) {
    if (!q || !task) {
        return TQ_RESULT_ERR;
    }

    ring_queue_result_t result = ring_queue_push(&q->ring, task);
    pthread_mutex_lock(&q->wait_lock);
    switch (result) {
    case RQ_RESULT_OK:
        q->stats.pushes++;
        pthread_mutex_unlock(&q->wait_lock);
        task_queue_signal(q);
        return TQ_RESULT_OK;
    case RQ_RESULT_MERGED:
        q->stats.merges++;
        pthread_mutex_unlock(&q->wait_lock);
        task_queue_signal(q);
        return TQ_RESULT_MERGED;
    case RQ_RESULT_DROPPED:
        q->stats.drops++;
        pthread_mutex_unlock(&q->wait_lock);
        return TQ_RESULT_DROPPED;
    default:
        pthread_mutex_unlock(&q->wait_lock);
        return TQ_RESULT_ERR;
    }
}

int task_queue_try_pop(task_queue_t *q, ubus_task_t *out) {
    if (!q || !out) {
        return -1;
    }
    bool got = ring_queue_pop(&q->ring, out);
    if (got) {
        pthread_mutex_lock(&q->wait_lock);
        q->stats.pops++;
        pthread_mutex_unlock(&q->wait_lock);
        return 1;
    }
    return 0;
}

static int task_queue_timed_wait(task_queue_t *q, uint64_t seq, int timeout_ms) {
    if (timeout_ms < 0) {
        while (atomic_load(&q->seq) == seq && !atomic_load(&q->closed)) {
            pthread_cond_wait(&q->wait_cond, &q->wait_lock);
        }
        return 0;
    }

    struct timespec ts;
    clockid_t clock_id = CLOCK_REALTIME;
#ifdef __linux__
    if (q->wait_clock_monotonic) {
        clock_id = CLOCK_MONOTONIC;
    }
#endif
    clock_gettime(clock_id, &ts);
    uint64_t nsec = (uint64_t)ts.tv_nsec + (uint64_t)timeout_ms * 1000000ULL;
    ts.tv_sec += (time_t)(nsec / 1000000000ULL);
    ts.tv_nsec = (long)(nsec % 1000000000ULL);

    while (atomic_load(&q->seq) == seq && !atomic_load(&q->closed)) {
        int rc = pthread_cond_timedwait(&q->wait_cond, &q->wait_lock, &ts);
        if (rc == ETIMEDOUT) {
            return 1;
        }
    }

    return 0;
}

int task_queue_wait(task_queue_t *q, ubus_task_t *out, int timeout_ms) {
    if (!q || !out) {
        return -1;
    }

    uint64_t seq = atomic_load(&q->seq);
    for (;;) {
        if (ring_queue_pop(&q->ring, out)) {
            pthread_mutex_lock(&q->wait_lock);
            q->stats.pops++;
            pthread_mutex_unlock(&q->wait_lock);
            return 1;
        }
        if (atomic_load(&q->closed)) {
            return 0;
        }

        if (timeout_ms == 0) {
            return 0;
        }

        pthread_mutex_lock(&q->wait_lock);
        if (ring_queue_pop(&q->ring, out)) {
            q->stats.pops++;
            pthread_mutex_unlock(&q->wait_lock);
            return 1;
        }
        if (atomic_load(&q->closed)) {
            pthread_mutex_unlock(&q->wait_lock);
            return 0;
        }

        if (atomic_load(&q->seq) != seq) {
            seq = atomic_load(&q->seq);
            pthread_mutex_unlock(&q->wait_lock);
            continue;
        }

        int timed_out = task_queue_timed_wait(q, seq, timeout_ms);
        seq = atomic_load(&q->seq);
        pthread_mutex_unlock(&q->wait_lock);

        if (timed_out) {
            return 0;
        }
    }
}

bool task_queue_is_expired(const ubus_task_t *task, uint64_t now_ms) {
    if (!task || task->timeout_ms == 0) {
        return false;
    }
    return (now_ms - task->enqueue_time_ms) > task->timeout_ms;
}

task_queue_stats_t task_queue_get_stats(task_queue_t *q) {
    task_queue_stats_t stats = {0};
    if (!q) {
        return stats;
    }
    pthread_mutex_lock(&q->wait_lock);
    stats = q->stats;
    pthread_mutex_unlock(&q->wait_lock);
    return stats;
}

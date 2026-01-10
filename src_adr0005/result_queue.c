#define _POSIX_C_SOURCE 200809L

#include "result_queue.h"

#include <errno.h>
#include <string.h>
#include <time.h>

static void result_queue_signal(result_queue_t *q) {
    atomic_fetch_add(&q->seq, 1);
    pthread_mutex_lock(&q->wait_lock);
    pthread_cond_signal(&q->wait_cond);
    pthread_mutex_unlock(&q->wait_lock);
}

static bool result_queue_is_abandoned(result_queue_t *q, uint32_t request_id) {
    for (size_t i = 0; i < q->abandoned_count; i++) {
        if (q->abandoned_ids[i] == request_id) {
            return true;
        }
    }
    return false;
}

static bool result_queue_match_abandoned(const void *item, void *user) {
    result_queue_t *q = (result_queue_t *)user;
    const ubus_result_t *res = (const ubus_result_t *)item;
    return result_queue_is_abandoned(q, res->request_id);
}

int result_queue_init(result_queue_t *q, size_t capacity) {
    if (!q || capacity == 0) {
        return -1;
    }

    memset(q, 0, sizeof(*q));
    if (ring_queue_init(&q->ring, capacity, sizeof(ubus_result_t)) != 0) {
        return -1;
    }
    ring_queue_set_overflow_policy(&q->ring, RQ_REJECT_NEW);

    pthread_mutex_init(&q->wait_lock, NULL);
    pthread_mutex_init(&q->abandoned_lock, NULL);
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

void result_queue_destroy(result_queue_t *q) {
    if (!q) {
        return;
    }
    ring_queue_destroy(&q->ring);
    pthread_mutex_destroy(&q->wait_lock);
    pthread_mutex_destroy(&q->abandoned_lock);
    pthread_cond_destroy(&q->wait_cond);
    memset(q, 0, sizeof(*q));
}

void result_queue_close(result_queue_t *q) {
    if (!q) {
        return;
    }
    atomic_store(&q->closed, true);
    result_queue_signal(q);
}

result_queue_result_t result_queue_push(result_queue_t *q, const ubus_result_t *result) {
    if (!q || !result) {
        return RQ_ERR;
    }

    pthread_mutex_lock(&q->abandoned_lock);
    bool is_abandoned = result_queue_is_abandoned(q, result->request_id);
    pthread_mutex_unlock(&q->abandoned_lock);

    if (is_abandoned) {
        pthread_mutex_lock(&q->wait_lock);
        q->stats.drops_abandoned++;
        pthread_mutex_unlock(&q->wait_lock);
        return RQ_DROPPED;
    }

    ring_queue_result_t rq_result = ring_queue_push(&q->ring, result);
    if (rq_result == RQ_RESULT_OK) {
        pthread_mutex_lock(&q->wait_lock);
        q->stats.pushes++;
        pthread_mutex_unlock(&q->wait_lock);
        result_queue_signal(q);
        return RQ_OK;
    }

    if (rq_result == RQ_RESULT_DROPPED) {
        pthread_mutex_lock(&q->abandoned_lock);
        bool replaced = ring_queue_replace_first_if(&q->ring, result_queue_match_abandoned, q, result);
        pthread_mutex_unlock(&q->abandoned_lock);

        if (replaced) {
            pthread_mutex_lock(&q->wait_lock);
            q->stats.pushes++;
            q->stats.drops_abandoned++;
            pthread_mutex_unlock(&q->wait_lock);
            result_queue_signal(q);
            return RQ_OK;
        }

        pthread_mutex_lock(&q->wait_lock);
        q->stats.drops++;
        pthread_mutex_unlock(&q->wait_lock);
        return RQ_DROPPED;
    }

    return RQ_ERR;
}

int result_queue_try_pop(result_queue_t *q, ubus_result_t *out) {
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

static int result_queue_timed_wait(result_queue_t *q, uint64_t seq, int timeout_ms) {
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

int result_queue_wait(result_queue_t *q, ubus_result_t *out, int timeout_ms) {
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

        int timed_out = result_queue_timed_wait(q, seq, timeout_ms);
        seq = atomic_load(&q->seq);
        pthread_mutex_unlock(&q->wait_lock);

        if (timed_out) {
            return 0;
        }
    }
}

void result_queue_mark_abandoned(result_queue_t *q, uint32_t request_id) {
    if (!q) {
        return;
    }
    pthread_mutex_lock(&q->abandoned_lock);
    if (q->abandoned_count < RESULT_QUEUE_MAX_ABANDONED) {
        for (size_t i = 0; i < q->abandoned_count; i++) {
            if (q->abandoned_ids[i] == request_id) {
                pthread_mutex_unlock(&q->abandoned_lock);
                return;
            }
        }
        q->abandoned_ids[q->abandoned_count++] = request_id;
    }
    pthread_mutex_unlock(&q->abandoned_lock);
}

void result_queue_clear_abandoned(result_queue_t *q, uint32_t request_id) {
    if (!q) {
        return;
    }
    pthread_mutex_lock(&q->abandoned_lock);
    for (size_t i = 0; i < q->abandoned_count; i++) {
        if (q->abandoned_ids[i] == request_id) {
            q->abandoned_ids[i] = q->abandoned_ids[q->abandoned_count - 1];
            q->abandoned_count--;
            break;
        }
    }
    pthread_mutex_unlock(&q->abandoned_lock);
}

result_queue_stats_t result_queue_get_stats(result_queue_t *q) {
    result_queue_stats_t stats = {0};
    if (!q) {
        return stats;
    }
    pthread_mutex_lock(&q->wait_lock);
    stats = q->stats;
    pthread_mutex_unlock(&q->wait_lock);
    return stats;
}

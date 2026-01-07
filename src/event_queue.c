#include "event_queue.h"

#include <errno.h>
#include <string.h>
#include <time.h>

static bool event_queue_is_tick(const app_event_t *event) {
    return event && event->type == EVT_TICK;
}

static bool event_queue_match_tick(const void *item, void *user) {
    (void)user;
    const app_event_t *evt = (const app_event_t *)item;
    return evt->type == EVT_TICK;
}

static bool event_queue_merge_tick(void *existing, const void *incoming, void *user) {
    (void)user;
    const app_event_t *in = (const app_event_t *)incoming;
    if (in->type != EVT_TICK) {
        return false;
    }
    app_event_t *ex = (app_event_t *)existing;
    if (ex->type != EVT_TICK) {
        return false;
    }
    ex->data += in->data;
    ex->timestamp_ns = in->timestamp_ns;
    return true;
}

static void event_queue_signal(event_queue_t *q) {
    atomic_fetch_add(&q->seq, 1);
    pthread_mutex_lock(&q->wait_lock);
    pthread_cond_signal(&q->wait_cond);
    pthread_mutex_unlock(&q->wait_lock);
}

int event_queue_init(event_queue_t *q, size_t capacity) {
    if (!q || capacity == 0) {
        return -1;
    }

    memset(q, 0, sizeof(*q));
    if (ring_queue_init(&q->ring, capacity, sizeof(app_event_t)) != 0) {
        return -1;
    }
    ring_queue_set_overflow_policy(&q->ring, RQ_COALESCE);
    ring_queue_set_merge_fn(&q->ring, event_queue_merge_tick, NULL);

    pthread_mutex_init(&q->wait_lock, NULL);
    pthread_cond_init(&q->wait_cond, NULL);
    atomic_store(&q->seq, 0);
    atomic_store(&q->closed, false);
    return 0;
}

void event_queue_destroy(event_queue_t *q) {
    if (!q) {
        return;
    }
    ring_queue_destroy(&q->ring);
    pthread_mutex_destroy(&q->wait_lock);
    pthread_cond_destroy(&q->wait_cond);
    memset(q, 0, sizeof(*q));
}

void event_queue_close(event_queue_t *q) {
    if (!q) {
        return;
    }
    atomic_store(&q->closed, true);
    event_queue_signal(q);
}

event_queue_result_t event_queue_push(event_queue_t *q, const app_event_t *event) {
    if (!q || !event) {
        return EQ_RESULT_ERR;
    }

    ring_queue_result_t result = ring_queue_push(&q->ring, event);
    if (result == RQ_RESULT_OK || result == RQ_RESULT_MERGED) {
        event_queue_signal(q);
        return EQ_RESULT_OK;
    }

    if (result == RQ_RESULT_DROPPED) {
        if (event_queue_is_tick(event)) {
            return EQ_RESULT_DROPPED;
        }

        if (ring_queue_replace_first_if(&q->ring, event_queue_match_tick, NULL, event)) {
            q->replaced_ticks++;
            event_queue_signal(q);
            return EQ_RESULT_REPLACED;
        }

        q->dropped_critical++;
        return EQ_RESULT_DROPPED;
    }

    return EQ_RESULT_ERR;
}

int event_queue_try_pop(event_queue_t *q, app_event_t *out) {
    if (!q || !out) {
        return -1;
    }
    return ring_queue_pop(&q->ring, out) ? 1 : 0;
}

static int event_queue_timed_wait(event_queue_t *q, uint64_t seq, int timeout_ms) {
    if (timeout_ms < 0) {
        while (atomic_load(&q->seq) == seq && !atomic_load(&q->closed)) {
            pthread_cond_wait(&q->wait_cond, &q->wait_lock);
        }
        return 0;
    }

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
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

int event_queue_wait(event_queue_t *q, app_event_t *out, int timeout_ms) {
    if (!q || !out) {
        return -1;
    }

    uint64_t seq = atomic_load(&q->seq);
    for (;;) {
        if (ring_queue_pop(&q->ring, out)) {
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

        int timed_out = event_queue_timed_wait(q, seq, timeout_ms);
        seq = atomic_load(&q->seq);
        pthread_mutex_unlock(&q->wait_lock);

        if (timed_out) {
            return 0;
        }
    }
}

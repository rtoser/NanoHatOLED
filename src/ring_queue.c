#include "ring_queue.h"

#include <stdlib.h>
#include <string.h>

static void ring_queue_lock(ring_queue_t *q) {
    pthread_mutex_lock(&q->lock);
}

static void ring_queue_unlock(ring_queue_t *q) {
    pthread_mutex_unlock(&q->lock);
}

static void *ring_queue_item_ptr(ring_queue_t *q, size_t index) {
    return q->buffer + (index * q->item_size);
}

int ring_queue_init(ring_queue_t *q, size_t capacity, size_t item_size) {
    if (!q || capacity == 0 || item_size == 0) {
        return -1;
    }

    memset(q, 0, sizeof(*q));
    q->buffer = (uint8_t *)malloc(capacity * item_size);
    if (!q->buffer) {
        return -1;
    }

    q->capacity = capacity;
    q->item_size = item_size;
    q->policy = RQ_OVERWRITE_OLDEST;
    pthread_mutex_init(&q->lock, NULL);
    return 0;
}

void ring_queue_destroy(ring_queue_t *q) {
    if (!q) {
        return;
    }
    pthread_mutex_destroy(&q->lock);
    free(q->buffer);
    q->buffer = NULL;
    q->capacity = 0;
    q->item_size = 0;
    q->head = 0;
    q->tail = 0;
    q->count = 0;
    q->merge_fn = NULL;
    q->merge_user = NULL;
    memset(&q->stats, 0, sizeof(q->stats));
}

void ring_queue_set_overflow_policy(ring_queue_t *q, ring_queue_overflow_policy_t policy) {
    if (!q) {
        return;
    }
    ring_queue_lock(q);
    q->policy = policy;
    ring_queue_unlock(q);
}

void ring_queue_set_merge_fn(ring_queue_t *q, ring_queue_merge_fn fn, void *user) {
    if (!q) {
        return;
    }
    ring_queue_lock(q);
    q->merge_fn = fn;
    q->merge_user = user;
    ring_queue_unlock(q);
}

static ring_queue_result_t ring_queue_try_merge(ring_queue_t *q, const void *item) {
    if (q->policy != RQ_COALESCE || !q->merge_fn || q->count == 0) {
        return RQ_RESULT_ERR;
    }

    for (size_t i = 0; i < q->count; i++) {
        size_t idx = (q->head + i) % q->capacity;
        void *existing = ring_queue_item_ptr(q, idx);
        if (q->merge_fn(existing, item, q->merge_user)) {
            q->stats.merges++;
            return RQ_RESULT_MERGED;
        }
    }

    return RQ_RESULT_ERR;
}

ring_queue_result_t ring_queue_push(ring_queue_t *q, const void *item) {
    if (!q || !item || !q->buffer) {
        return RQ_RESULT_ERR;
    }

    ring_queue_lock(q);
    q->stats.pushes++;

    if (ring_queue_try_merge(q, item) == RQ_RESULT_MERGED) {
        ring_queue_unlock(q);
        return RQ_RESULT_MERGED;
    }

    if (q->count == q->capacity) {
        if (q->policy == RQ_OVERWRITE_OLDEST) {
            memcpy(ring_queue_item_ptr(q, q->tail), item, q->item_size);
            q->tail = (q->tail + 1) % q->capacity;
            q->head = q->tail;
            q->stats.overwrites++;
            ring_queue_unlock(q);
            return RQ_RESULT_OK;
        }

        q->stats.drops++;
        ring_queue_unlock(q);
        return RQ_RESULT_DROPPED;
    }

    memcpy(ring_queue_item_ptr(q, q->tail), item, q->item_size);
    q->tail = (q->tail + 1) % q->capacity;
    q->count++;
    ring_queue_unlock(q);
    return RQ_RESULT_OK;
}

bool ring_queue_pop(ring_queue_t *q, void *out) {
    if (!q || !out || !q->buffer) {
        return false;
    }

    ring_queue_lock(q);
    if (q->count == 0) {
        ring_queue_unlock(q);
        return false;
    }

    memcpy(out, ring_queue_item_ptr(q, q->head), q->item_size);
    q->head = (q->head + 1) % q->capacity;
    q->count--;
    q->stats.pops++;
    ring_queue_unlock(q);
    return true;
}

size_t ring_queue_count(ring_queue_t *q) {
    if (!q) {
        return 0;
    }
    ring_queue_lock(q);
    size_t count = q->count;
    ring_queue_unlock(q);
    return count;
}

size_t ring_queue_capacity(ring_queue_t *q) {
    if (!q) {
        return 0;
    }
    ring_queue_lock(q);
    size_t capacity = q->capacity;
    ring_queue_unlock(q);
    return capacity;
}

ring_queue_stats_t ring_queue_get_stats(ring_queue_t *q) {
    ring_queue_stats_t stats = {0};
    if (!q) {
        return stats;
    }
    ring_queue_lock(q);
    stats = q->stats;
    ring_queue_unlock(q);
    return stats;
}

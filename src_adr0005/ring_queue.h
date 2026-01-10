#ifndef RING_QUEUE_H
#define RING_QUEUE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <pthread.h>

typedef enum {
    RQ_OVERWRITE_OLDEST = 0,
    RQ_REJECT_NEW = 1,
    RQ_COALESCE = 2
} ring_queue_overflow_policy_t;

typedef enum {
    RQ_RESULT_OK = 0,
    RQ_RESULT_MERGED = 1,
    RQ_RESULT_DROPPED = 2,
    RQ_RESULT_ERR = -1
} ring_queue_result_t;

typedef bool (*ring_queue_merge_fn)(void *existing, const void *incoming, void *user);
typedef bool (*ring_queue_match_fn)(const void *item, void *user);

typedef struct {
    uint64_t pushes;
    uint64_t pops;
    uint64_t overwrites;
    uint64_t drops;
    uint64_t merges;
} ring_queue_stats_t;

typedef struct {
    uint8_t *buffer;
    size_t item_size;
    size_t capacity;
    size_t head;
    size_t tail;
    size_t count;
    ring_queue_overflow_policy_t policy;
    ring_queue_merge_fn merge_fn;
    void *merge_user;
    ring_queue_stats_t stats;
    pthread_mutex_t lock;
} ring_queue_t;

int ring_queue_init(ring_queue_t *q, size_t capacity, size_t item_size);
void ring_queue_destroy(ring_queue_t *q);

void ring_queue_set_overflow_policy(ring_queue_t *q, ring_queue_overflow_policy_t policy);
void ring_queue_set_merge_fn(ring_queue_t *q, ring_queue_merge_fn fn, void *user);

ring_queue_result_t ring_queue_push(ring_queue_t *q, const void *item);
bool ring_queue_pop(ring_queue_t *q, void *out);
bool ring_queue_replace_first_if(ring_queue_t *q, ring_queue_match_fn match, void *user, const void *item);

size_t ring_queue_count(ring_queue_t *q);
size_t ring_queue_capacity(ring_queue_t *q);
ring_queue_stats_t ring_queue_get_stats(ring_queue_t *q);

#endif

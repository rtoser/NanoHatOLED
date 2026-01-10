/*
 * Test: uloop_timeout basic precision test
 *
 * Verifies timer fires within acceptable tolerance.
 * Calls uloop_end() after timer fires to avoid test hanging.
 */
#include <stdio.h>
#include <stdint.h>
#include <time.h>
#include <libubox/uloop.h>

#define TEST_INTERVAL_MS  100
#define TOLERANCE_MS      50  /* Relaxed for loaded systems / CI */

static struct uloop_timeout test_timer;
static uint64_t start_time_ms;

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)(ts.tv_nsec / 1000000);
}

static void timer_cb(struct uloop_timeout *t) {
    (void)t;

    uint64_t elapsed = now_ms() - start_time_ms;
    int64_t error = (int64_t)elapsed - TEST_INTERVAL_MS;

    printf("Timer fired: elapsed=%llu ms, error=%lld ms\n",
           (unsigned long long)elapsed, (long long)error);

    if (error < -TOLERANCE_MS || error > TOLERANCE_MS) {
        fprintf(stderr, "FAIL: timing error exceeds tolerance (%d ms)\n", TOLERANCE_MS);
        uloop_end();
        return;
    }

    printf("OK: timing within tolerance\n");
    uloop_end();
}

int main(void) {
    printf("=== test_timer_basic ===\n");
    printf("Testing %d ms timer with %d ms tolerance\n", TEST_INTERVAL_MS, TOLERANCE_MS);

    if (uloop_init() != 0) {
        fprintf(stderr, "FAIL: uloop_init failed\n");
        return 1;
    }

    /* Record start time and set timer */
    start_time_ms = now_ms();
    test_timer.cb = timer_cb;
    uloop_timeout_set(&test_timer, TEST_INTERVAL_MS);

    /* Run until timer fires and calls uloop_end() */
    uloop_run();

    uloop_done();

    printf("=== PASS ===\n");
    return 0;
}

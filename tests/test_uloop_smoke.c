/*
 * Test: uloop smoke test
 *
 * Verifies uloop can initialize and shutdown cleanly.
 */
#include <stdio.h>
#include <signal.h>
#include <libubox/uloop.h>

static struct uloop_timeout shutdown_timer;

static void shutdown_cb(struct uloop_timeout *t) {
    (void)t;
    printf("Timeout fired, calling uloop_end()\n");
    uloop_end();
}

int main(void) {
    printf("=== test_uloop_smoke ===\n");

    /* Initialize uloop */
    if (uloop_init() != 0) {
        fprintf(stderr, "FAIL: uloop_init failed\n");
        return 1;
    }
    printf("OK: uloop_init\n");

    /* Set a short timeout to exit the loop */
    shutdown_timer.cb = shutdown_cb;
    uloop_timeout_set(&shutdown_timer, 100); /* 100ms */
    printf("OK: timer set for 100ms\n");

    /* Run the loop (should exit after timeout) */
    printf("Running uloop...\n");
    uloop_run();
    printf("OK: uloop_run returned\n");

    /* Cleanup */
    uloop_done();
    printf("OK: uloop_done\n");

    printf("=== PASS ===\n");
    return 0;
}

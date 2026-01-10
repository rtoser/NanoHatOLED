/*
 * Test: ubus async integration with uloop
 *
 * Tests async service queries using mock ubus HAL.
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>
#include <libubox/uloop.h>

#include "hal/ubus_hal.h"
#include "sys_status.h"
#include "service_config.h"

/* Test injection API from mock */
extern void ubus_mock_set_response(const char *service, int status,
                                    bool installed, bool running, int delay_ms);
extern void ubus_mock_set_default_response(int status, bool installed,
                                            bool running, int delay_ms);
extern void ubus_mock_set_timeout(int timeout_ms);
extern void ubus_mock_clear_responses(void);
extern int ubus_mock_get_pending_count(void);

/* Special delay value: never respond (for timeout testing) */
#define MOCK_DELAY_HANG (-1)

/* Test state */
static int g_callbacks_received = 0;
static bool g_test_passed = false;
static struct uloop_timeout g_timeout;

/*
 * Test 1: Basic async query with mock
 */
static void test1_cb(const char *service, bool installed, bool running,
                     int status, void *priv) {
    (void)priv;
    printf("  Callback: service=%s installed=%d running=%d status=%d\n",
           service, installed, running, status);

    g_callbacks_received++;

    if (strcmp(service, "dropbear") == 0) {
        assert(status == UBUS_HAL_STATUS_OK);
        assert(installed == true);
        assert(running == true);
        g_test_passed = true;
    }
}

static void test1_timeout_cb(struct uloop_timeout *t) {
    (void)t;
    printf("  Test timeout - stopping uloop\n");
    uloop_end();
}

static void test_basic_query(void) {
    printf("\n=== Test: Basic async query ===\n");

    g_callbacks_received = 0;
    g_test_passed = false;

    ubus_mock_clear_responses();
    ubus_mock_set_response("dropbear", UBUS_HAL_STATUS_OK, true, true, 50);

    int ret = ubus_hal->query_service_async("dropbear", test1_cb, NULL);
    assert(ret == 0);

    /* Set timeout to stop test */
    g_timeout.cb = test1_timeout_cb;
    uloop_timeout_set(&g_timeout, 200);

    /* Run uloop until callback or timeout */
    uloop_run();

    assert(g_callbacks_received == 1);
    assert(g_test_passed);

    printf("  PASSED\n");
}

/*
 * Test 2: Query timeout (using HANG mode to trigger timeout protection)
 *
 * Sets delay_ms = MOCK_DELAY_HANG (-1) which prevents response_timer from firing.
 * The timeout_timer fires after the configured timeout and returns TIMEOUT status.
 */
static void test2_cb(const char *service, bool installed, bool running,
                     int status, void *priv) {
    (void)priv;
    (void)installed;
    (void)running;
    printf("  Callback: service=%s status=%d (expect TIMEOUT=%d)\n",
           service, status, UBUS_HAL_STATUS_TIMEOUT);

    g_callbacks_received++;

    if (strcmp(service, "hang_svc") == 0) {
        assert(status == UBUS_HAL_STATUS_TIMEOUT);
        g_test_passed = true;
        uloop_end();  /* Got expected timeout, stop test */
    }
}

static void test2_timeout_cb(struct uloop_timeout *t) {
    (void)t;
    printf("  Test guard timeout - stopping (should not reach here)\n");
    uloop_end();
}

static void test_query_timeout(void) {
    printf("\n=== Test: Query timeout (HANG mode) ===\n");

    g_callbacks_received = 0;
    g_test_passed = false;

    ubus_mock_clear_responses();

    /* Set short timeout for faster test */
    ubus_mock_set_timeout(100);

    /* HANG mode: response timer never fires, timeout timer will trigger */
    ubus_mock_set_response("hang_svc", UBUS_HAL_STATUS_OK, true, true, MOCK_DELAY_HANG);

    int ret = ubus_hal->query_service_async("hang_svc", test2_cb, NULL);
    assert(ret == 0);

    /* Guard timeout in case something goes wrong */
    g_timeout.cb = test2_timeout_cb;
    uloop_timeout_set(&g_timeout, 500);

    uloop_run();

    assert(g_callbacks_received == 1);
    assert(g_test_passed);

    /* Reset timeout to default */
    ubus_mock_set_timeout(3000);

    printf("  PASSED\n");
}

/*
 * Test 3: Multiple concurrent queries
 */
static int g_multi_cb_count = 0;
static bool g_multi_results[3] = {false, false, false};

static void test3_cb(const char *service, bool installed, bool running,
                     int status, void *priv) {
    (void)priv;
    printf("  Callback: service=%s installed=%d running=%d status=%d\n",
           service, installed, running, status);

    g_multi_cb_count++;

    if (strcmp(service, "svc1") == 0 && installed && running) {
        g_multi_results[0] = true;
    } else if (strcmp(service, "svc2") == 0 && installed && !running) {
        g_multi_results[1] = true;
    } else if (strcmp(service, "svc3") == 0 && !installed) {
        g_multi_results[2] = true;
    }

    if (g_multi_cb_count >= 3) {
        uloop_end();
    }
}

static void test3_timeout_cb(struct uloop_timeout *t) {
    (void)t;
    printf("  Test timeout - stopping (received %d callbacks)\n", g_multi_cb_count);
    uloop_end();
}

static void test_multiple_queries(void) {
    printf("\n=== Test: Multiple concurrent queries ===\n");

    g_multi_cb_count = 0;
    memset(g_multi_results, 0, sizeof(g_multi_results));

    ubus_mock_clear_responses();
    ubus_mock_set_response("svc1", UBUS_HAL_STATUS_OK, true, true, 30);
    ubus_mock_set_response("svc2", UBUS_HAL_STATUS_OK, true, false, 60);
    ubus_mock_set_response("svc3", UBUS_HAL_STATUS_OK, false, false, 90);

    /* Issue all queries */
    assert(ubus_hal->query_service_async("svc1", test3_cb, NULL) == 0);
    assert(ubus_hal->query_service_async("svc2", test3_cb, NULL) == 0);
    assert(ubus_hal->query_service_async("svc3", test3_cb, NULL) == 0);

    g_timeout.cb = test3_timeout_cb;
    uloop_timeout_set(&g_timeout, 500);

    uloop_run();

    assert(g_multi_cb_count == 3);
    assert(g_multi_results[0] && g_multi_results[1] && g_multi_results[2]);

    printf("  PASSED\n");
}

/*
 * Test 4: Callback at most once (late response after timeout)
 *
 * Verifies that if a "late" response arrives after timeout has already
 * fired, the callback is NOT invoked again.
 */
static int g_callback_count = 0;

static void test5_cb(const char *service, bool installed, bool running,
                     int status, void *priv) {
    (void)service;
    (void)installed;
    (void)running;
    (void)status;
    (void)priv;
    g_callback_count++;
    printf("  Callback invoked (count=%d)\n", g_callback_count);
}

extern int ubus_mock_force_late_response(const char *service);

static void test5_timeout_cb(struct uloop_timeout *t) {
    (void)t;
    uloop_end();
}

static void test_callback_at_most_once(void) {
    printf("\n=== Test: Callback at most once ===\n");

    g_callback_count = 0;

    ubus_mock_clear_responses();
    ubus_mock_set_timeout(50);  /* Short timeout */

    /* HANG mode: timeout will fire, not response */
    ubus_mock_set_response("late_svc", UBUS_HAL_STATUS_OK, true, true, MOCK_DELAY_HANG);

    int ret = ubus_hal->query_service_async("late_svc", test5_cb, NULL);
    assert(ret == 0);

    /* Wait for timeout to fire */
    g_timeout.cb = test5_timeout_cb;
    uloop_timeout_set(&g_timeout, 150);
    uloop_run();

    assert(g_callback_count == 1);
    printf("  Callback count after timeout: %d\n", g_callback_count);

    /* Simulate late response (should be ignored due to completed flag) */
    int late_attempted = ubus_mock_force_late_response("late_svc");
    printf("  Late response attempted: %d\n", late_attempted);

    /* Verify the late response scenario was actually attempted */
    assert(late_attempted == 1);

    /* Callback count should still be 1 (late response was ignored) */
    assert(g_callback_count == 1);

    ubus_mock_set_timeout(3000);  /* Reset */

    printf("  PASSED\n");
}

/*
 * Test 5: Consecutive timeouts tracking
 *
 * Verifies that consecutive timeouts are tracked correctly.
 * In real impl, this triggers reset_connection after TIMEOUT_RESET_THRESHOLD.
 */
static int g_timeout_count = 0;

static void test6_cb(const char *service, bool installed, bool running,
                     int status, void *priv) {
    (void)service;
    (void)installed;
    (void)running;
    (void)priv;
    if (status == UBUS_HAL_STATUS_TIMEOUT) {
        g_timeout_count++;
    }
}

extern int ubus_mock_get_consecutive_timeouts(void);

static void test6_timeout_cb(struct uloop_timeout *t) {
    (void)t;
    uloop_end();
}

static void test_consecutive_timeouts(void) {
    printf("\n=== Test: Consecutive timeouts tracking ===\n");

    g_timeout_count = 0;

    ubus_mock_clear_responses();
    ubus_mock_set_timeout(30);  /* Short timeout */

    /* All requests use HANG mode to trigger timeout */
    ubus_mock_set_response("svc1", UBUS_HAL_STATUS_OK, true, true, MOCK_DELAY_HANG);
    ubus_mock_set_response("svc2", UBUS_HAL_STATUS_OK, true, true, MOCK_DELAY_HANG);
    ubus_mock_set_response("svc3", UBUS_HAL_STATUS_OK, true, true, MOCK_DELAY_HANG);

    /* Issue 3 requests that will all timeout */
    assert(ubus_hal->query_service_async("svc1", test6_cb, NULL) == 0);
    assert(ubus_hal->query_service_async("svc2", test6_cb, NULL) == 0);
    assert(ubus_hal->query_service_async("svc3", test6_cb, NULL) == 0);

    /* Wait for all timeouts */
    g_timeout.cb = test6_timeout_cb;
    uloop_timeout_set(&g_timeout, 200);
    uloop_run();

    printf("  Timeout callbacks received: %d\n", g_timeout_count);
    printf("  Consecutive timeouts tracked: %d\n", ubus_mock_get_consecutive_timeouts());

    /* All 3 should have timed out */
    assert(g_timeout_count == 3);

    /* Mock should track consecutive timeouts (real impl uses this for reset) */
    assert(ubus_mock_get_consecutive_timeouts() == 3);

    /* Verify a successful response resets the counter */
    ubus_mock_set_response("svc_ok", UBUS_HAL_STATUS_OK, true, true, 10);
    assert(ubus_hal->query_service_async("svc_ok", test6_cb, NULL) == 0);

    uloop_timeout_set(&g_timeout, 100);
    uloop_run();

    assert(ubus_mock_get_consecutive_timeouts() == 0);
    printf("  After success, consecutive timeouts reset to: %d\n",
           ubus_mock_get_consecutive_timeouts());

    ubus_mock_set_timeout(3000);  /* Reset */

    printf("  PASSED\n");
}

/*
 * Test 6: sys_status integration
 */
static void test4_timeout_cb(struct uloop_timeout *t) {
    (void)t;
    uloop_end();
}

static void test_sys_status_integration(void) {
    printf("\n=== Test: sys_status integration ===\n");

    ubus_mock_clear_responses();
    ubus_mock_set_response("dropbear", UBUS_HAL_STATUS_OK, true, true, 30);
    ubus_mock_set_response("uhttpd", UBUS_HAL_STATUS_OK, true, false, 50);

    /* Initialize service config */
    service_config_t config;
    service_config_init(&config);

    /* Create status with services */
    sys_status_t status;
    memset(&status, 0, sizeof(status));

    /* Manually populate services (normally done by sys_status_update_local) */
    const service_config_t *cfg = service_config_get();
    if (cfg) {
        for (size_t i = 0; i < cfg->count && i < MAX_SERVICES; i++) {
            strncpy(status.services[i].name, cfg->services[i].name,
                    sizeof(status.services[i].name) - 1);
            status.services[i].query_pending = false;
            status.services[i].status_valid = false;
        }
        status.service_count = cfg->count;
    }

    /* Initiate queries */
    int queries = sys_status_query_services(NULL, &status);
    printf("  Queries initiated: %d\n", queries);

    /* Check pending state */
    assert(sys_status_has_pending_queries(&status));

    /* Run uloop to process callbacks */
    g_timeout.cb = test4_timeout_cb;
    uloop_timeout_set(&g_timeout, 200);
    uloop_run();

    /* Check results */
    printf("  Results:\n");
    for (size_t i = 0; i < status.service_count; i++) {
        printf("    %s: valid=%d installed=%d running=%d pending=%d\n",
               status.services[i].name,
               status.services[i].status_valid,
               status.services[i].installed,
               status.services[i].running,
               status.services[i].query_pending);
    }

    /* Verify no longer pending */
    assert(!sys_status_has_pending_queries(&status));

    printf("  PASSED\n");
}

int main(void) {
    printf("=== ubus async uloop tests ===\n");

    /* Initialize uloop */
    uloop_init();

    /* Initialize ubus HAL (mock) */
    int ret = ubus_hal->init();
    assert(ret == 0);

    /* Run tests */
    test_basic_query();
    test_query_timeout();
    test_multiple_queries();
    test_callback_at_most_once();
    test_consecutive_timeouts();
    test_sys_status_integration();

    /* Cleanup */
    ubus_hal->cleanup();
    uloop_done();

    printf("\n=== All tests PASSED ===\n");
    return 0;
}

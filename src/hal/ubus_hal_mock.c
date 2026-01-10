/*
 * ubus HAL mock implementation for host testing
 *
 * Uses uloop_timeout to simulate async responses with configurable delays.
 * Includes timeout protection (matching real implementation).
 * Provides test injection API for controlling responses.
 */
#define _POSIX_C_SOURCE 200809L

#include "ubus_hal.h"

#include <string.h>
#include <stdlib.h>
#include <libubox/uloop.h>

#define MAX_PENDING_REQUESTS 16
#define MAX_MOCK_RESPONSES   16
#define DEFAULT_DELAY_MS     50
#define DEFAULT_TIMEOUT_MS   3000

/* Special delay value: never respond (for timeout testing) */
#define MOCK_DELAY_HANG (-1)

/*
 * Mock response configuration (set via test injection API)
 */
typedef struct {
    char service[32];
    bool installed;
    bool running;
    int status;
    int delay_ms;
    bool configured;
} mock_response_t;

/*
 * Pending request state
 */
typedef struct {
    char service[32];
    ubus_query_cb cb;
    void *priv;
    struct uloop_timeout response_timer;  /* Simulated response delay */
    struct uloop_timeout timeout_timer;   /* Timeout protection */
    bool in_use;
    bool completed;

    /* Resolved response */
    bool installed;
    bool running;
    int status;
} pending_request_t;

static mock_response_t g_mock_responses[MAX_MOCK_RESPONSES];
static pending_request_t g_pending[MAX_PENDING_REQUESTS];
static bool g_initialized = false;
static int g_timeout_ms = DEFAULT_TIMEOUT_MS;
static int g_consecutive_timeouts = 0;  /* Track consecutive timeouts for testing */

/* Default response for unconfigured services */
static mock_response_t g_default_response = {
    .installed = false,
    .running = false,
    .status = UBUS_HAL_STATUS_OK,
    .delay_ms = DEFAULT_DELAY_MS,
    .configured = false
};

/* Forward declarations */
static void response_timer_cb(struct uloop_timeout *t);
static void timeout_timer_cb(struct uloop_timeout *t);

/*
 * Test injection API
 */
void ubus_mock_set_response(const char *service, int status,
                            bool installed, bool running, int delay_ms) {
    /* Find existing or empty slot */
    int empty_slot = -1;
    for (int i = 0; i < MAX_MOCK_RESPONSES; i++) {
        if (g_mock_responses[i].configured &&
            strcmp(g_mock_responses[i].service, service) == 0) {
            /* Update existing */
            g_mock_responses[i].status = status;
            g_mock_responses[i].installed = installed;
            g_mock_responses[i].running = running;
            g_mock_responses[i].delay_ms = delay_ms;
            return;
        }
        if (!g_mock_responses[i].configured && empty_slot < 0) {
            empty_slot = i;
        }
    }

    /* Add new */
    if (empty_slot >= 0) {
        strncpy(g_mock_responses[empty_slot].service, service,
                sizeof(g_mock_responses[empty_slot].service) - 1);
        g_mock_responses[empty_slot].service[sizeof(g_mock_responses[empty_slot].service) - 1] = '\0';
        g_mock_responses[empty_slot].status = status;
        g_mock_responses[empty_slot].installed = installed;
        g_mock_responses[empty_slot].running = running;
        g_mock_responses[empty_slot].delay_ms = delay_ms;
        g_mock_responses[empty_slot].configured = true;
    }
}

void ubus_mock_set_default_response(int status, bool installed,
                                     bool running, int delay_ms) {
    g_default_response.status = status;
    g_default_response.installed = installed;
    g_default_response.running = running;
    g_default_response.delay_ms = delay_ms;
}

void ubus_mock_set_timeout(int timeout_ms) {
    g_timeout_ms = timeout_ms;
}

void ubus_mock_clear_responses(void) {
    memset(g_mock_responses, 0, sizeof(g_mock_responses));
    g_default_response.status = UBUS_HAL_STATUS_OK;
    g_default_response.installed = false;
    g_default_response.running = false;
    g_default_response.delay_ms = DEFAULT_DELAY_MS;
    g_timeout_ms = DEFAULT_TIMEOUT_MS;
    g_consecutive_timeouts = 0;
}

int ubus_mock_get_consecutive_timeouts(void) {
    return g_consecutive_timeouts;
}

int ubus_mock_get_pending_count(void) {
    int count = 0;
    for (int i = 0; i < MAX_PENDING_REQUESTS; i++) {
        if (g_pending[i].in_use && !g_pending[i].completed) count++;
    }
    return count;
}

/*
 * Force a "late" response for a service (for testing callback-at-most-once).
 * This simulates the scenario where ubus_abort_request() doesn't fully cancel
 * and a late complete_cb fires after timeout.
 *
 * Searches for a completed request by service name (regardless of in_use flag,
 * since in_use is cleared after timeout but service name remains).
 *
 * Returns 1 if a late response was attempted (to a completed request),
 * 0 if no matching request found.
 */
int ubus_mock_force_late_response(const char *service) {
    for (int i = 0; i < MAX_PENDING_REQUESTS; i++) {
        pending_request_t *req = &g_pending[i];
        /* Search by service name regardless of in_use (slot may be released) */
        if (req->completed && strcmp(req->service, service) == 0) {
            /*
             * Simulate late response_timer_cb firing.
             * The completed flag should cause it to return early without
             * invoking the callback again.
             */
            req->in_use = true;  /* Temporarily restore for the check */
            response_timer_cb(&req->response_timer);
            req->in_use = false;
            return 1;
        }
    }
    return 0;
}

/*
 * Find mock response for service
 */
static const mock_response_t *find_response(const char *service) {
    for (int i = 0; i < MAX_MOCK_RESPONSES; i++) {
        if (g_mock_responses[i].configured &&
            strcmp(g_mock_responses[i].service, service) == 0) {
            return &g_mock_responses[i];
        }
    }
    return &g_default_response;
}

/*
 * Response timer callback - simulated async response
 */
static void response_timer_cb(struct uloop_timeout *t) {
    pending_request_t *req = container_of(t, pending_request_t, response_timer);

    if (!req->in_use || req->completed) return;

    req->completed = true;
    uloop_timeout_cancel(&req->timeout_timer);

    /* Success resets consecutive timeout counter */
    g_consecutive_timeouts = 0;

    /* Invoke callback */
    if (req->cb) {
        req->cb(req->service, req->installed, req->running, req->status, req->priv);
    }

    /* Release slot */
    req->in_use = false;
}

/*
 * Timeout timer callback - fires when response takes too long
 */
static void timeout_timer_cb(struct uloop_timeout *t) {
    pending_request_t *req = container_of(t, pending_request_t, timeout_timer);

    if (!req->in_use || req->completed) return;

    req->completed = true;
    uloop_timeout_cancel(&req->response_timer);

    /* Track consecutive timeouts (mirrors real impl behavior) */
    g_consecutive_timeouts++;

    /* Invoke callback with timeout status */
    if (req->cb) {
        req->cb(req->service, false, false, UBUS_HAL_STATUS_TIMEOUT, req->priv);
    }

    /* Release slot */
    req->in_use = false;
}

/*
 * Allocate pending request slot
 */
static pending_request_t *alloc_request(void) {
    for (int i = 0; i < MAX_PENDING_REQUESTS; i++) {
        if (!g_pending[i].in_use) {
            memset(&g_pending[i], 0, sizeof(pending_request_t));
            g_pending[i].in_use = true;
            g_pending[i].completed = false;
            g_pending[i].response_timer.cb = response_timer_cb;
            g_pending[i].timeout_timer.cb = timeout_timer_cb;
            return &g_pending[i];
        }
    }
    return NULL;
}

/*
 * HAL implementation
 */
static int mock_init(void) {
    if (g_initialized) return 0;

    memset(g_pending, 0, sizeof(g_pending));
    g_initialized = true;
    return 0;
}

static void mock_cleanup(void) {
    if (!g_initialized) return;

    /* Cancel all pending timers */
    for (int i = 0; i < MAX_PENDING_REQUESTS; i++) {
        if (g_pending[i].in_use) {
            uloop_timeout_cancel(&g_pending[i].response_timer);
            uloop_timeout_cancel(&g_pending[i].timeout_timer);
            g_pending[i].in_use = false;
        }
    }

    g_initialized = false;
}

static int mock_query_service_async(const char *name, ubus_query_cb cb, void *priv) {
    if (!g_initialized || !name || !cb) return -1;

    pending_request_t *req = alloc_request();
    if (!req) {
        /* Match real impl: callback with error, return 0 */
        cb(name, false, false, UBUS_HAL_STATUS_ERROR, priv);
        return 0;
    }

    /* Copy service name */
    strncpy(req->service, name, sizeof(req->service) - 1);
    req->service[sizeof(req->service) - 1] = '\0';
    req->cb = cb;
    req->priv = priv;

    /* Look up mock response */
    const mock_response_t *resp = find_response(name);
    req->installed = resp->installed;
    req->running = resp->running;
    req->status = resp->status;

    /* Start timeout timer */
    uloop_timeout_set(&req->timeout_timer, g_timeout_ms);

    /* Schedule response callback (unless HANG mode) */
    if (resp->delay_ms != MOCK_DELAY_HANG) {
        uloop_timeout_set(&req->response_timer, resp->delay_ms);
    }
    /* HANG mode: response timer never fires, timeout will trigger */

    return 0;
}

static int mock_query_services_async(const char **names, size_t count,
                                      ubus_query_cb cb, void *priv) {
    if (!g_initialized || !names || !cb) return -1;

    /* Simple implementation: issue individual queries */
    for (size_t i = 0; i < count; i++) {
        if (names[i]) {
            int ret = mock_query_service_async(names[i], cb, priv);
            if (ret < 0) return -1;
        }
    }

    return 0;
}

static const ubus_hal_ops_t mock_ops = {
    .init = mock_init,
    .cleanup = mock_cleanup,
    .query_service_async = mock_query_service_async,
    .query_services_async = mock_query_services_async,
};

const ubus_hal_ops_t *ubus_hal = &mock_ops;

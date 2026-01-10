/*
 * ubus HAL real implementation for OpenWrt target
 *
 * Uses ubus_invoke_async + ubus_add_uloop for single-threaded async queries.
 * Features:
 *   - Request timeout protection via uloop_timeout
 *   - Lazy reconnect on rpcd restart (reset rc_id on error)
 */
#define _POSIX_C_SOURCE 200809L

#include "ubus_hal.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <libubus.h>
#include <libubox/uloop.h>
#include <libubox/blobmsg.h>

#define MAX_PENDING_REQUESTS 16
#define DEFAULT_TIMEOUT_MS   3000

/*
 * Pending request state - must persist until complete_cb or timeout
 */
typedef struct pending_request {
    struct ubus_request req;
    struct uloop_timeout timeout;
    char service[32];
    ubus_query_cb cb;
    void *priv;
    bool in_use;
    bool completed;  /* Set when complete_cb called, prevents double-free */

    /* Parsed response */
    bool installed;
    bool running;
} pending_request_t;

static struct ubus_context *g_ctx = NULL;
static uint32_t g_rc_id = 0;
static pending_request_t g_pending[MAX_PENDING_REQUESTS];
static bool g_initialized = false;

/* Reconnection backoff state */
static int g_consecutive_failures = 0;
static time_t g_last_attempt_time = 0;
#define BACKOFF_BASE_SEC  1
#define BACKOFF_MAX_SEC   60
#define TIMEOUT_RESET_THRESHOLD 3  /* Reset connection after N consecutive timeouts */

/*
 * Response parsing for "rc list"
 */
enum {
    RC_RUNNING,
    RC_ENABLED,
    __RC_MAX,
};

static const struct blobmsg_policy rc_policy[] = {
    [RC_RUNNING] = { .name = "running", .type = BLOBMSG_TYPE_BOOL },
    [RC_ENABLED] = { .name = "enabled", .type = BLOBMSG_TYPE_BOOL },
};

/* Forward declarations */
static void request_timeout_cb(struct uloop_timeout *t);
static void reset_connection(void);

/*
 * Allocate pending request slot
 */
static pending_request_t *alloc_request(void) {
    for (int i = 0; i < MAX_PENDING_REQUESTS; i++) {
        if (!g_pending[i].in_use) {
            memset(&g_pending[i], 0, sizeof(pending_request_t));
            g_pending[i].in_use = true;
            g_pending[i].completed = false;
            g_pending[i].timeout.cb = request_timeout_cb;
            return &g_pending[i];
        }
    }
    return NULL;
}

/*
 * Release pending request slot
 */
static void release_request(pending_request_t *preq) {
    if (preq) {
        uloop_timeout_cancel(&preq->timeout);
        preq->in_use = false;
    }
}

/*
 * Timeout callback - fires when request takes too long
 *
 * NOTE: We set completed=true BEFORE ubus_abort_request() to prevent
 * potential double-callback if abort synchronously triggers complete_cb.
 */
static void request_timeout_cb(struct uloop_timeout *t) {
    pending_request_t *preq = container_of(t, pending_request_t, timeout);

    if (!preq->in_use || preq->completed) return;

    /* Mark completed BEFORE abort to prevent double-callback */
    preq->completed = true;

    /* Abort the ubus request (safe even if already completed internally) */
    if (g_ctx) {
        ubus_abort_request(g_ctx, &preq->req);
    }

    /* Timeout counts as failure for backoff */
    g_consecutive_failures++;

    /*
     * After threshold consecutive timeouts, reset connection to trigger backoff.
     * This handles cases where connection is alive but rpcd is stuck.
     */
    if (g_consecutive_failures >= TIMEOUT_RESET_THRESHOLD) {
        reset_connection();
    } else {
        /* Just invalidate rc_id for lazy re-lookup */
        g_rc_id = 0;
    }

    /* Invoke user callback with timeout status */
    if (preq->cb) {
        preq->cb(preq->service, false, false, UBUS_HAL_STATUS_TIMEOUT, preq->priv);
    }

    /* Release slot */
    preq->in_use = false;
}

/*
 * Data callback - parse "rc list" response
 *
 * Response format: { "service_name": { "running": bool, "enabled": bool } }
 */
static void query_data_cb(struct ubus_request *req, int type, struct blob_attr *msg) {
    (void)type;
    pending_request_t *preq = container_of(req, pending_request_t, req);

    if (!msg) return;

    struct blob_attr *svc_attr;
    int rem;

    blobmsg_for_each_attr(svc_attr, msg, rem) {
        if (blobmsg_type(svc_attr) != BLOBMSG_TYPE_TABLE) continue;

        /* Service exists in rc */
        preq->installed = true;

        struct blob_attr *tb[__RC_MAX];
        blobmsg_parse(rc_policy, __RC_MAX, tb,
                      blobmsg_data(svc_attr), blobmsg_data_len(svc_attr));

        if (tb[RC_RUNNING] && blobmsg_get_bool(tb[RC_RUNNING])) {
            preq->running = true;
        }

        /* Only one service expected per query */
        return;
    }
}

/*
 * Complete callback - invoked when request finishes (success or error)
 */
static void query_complete_cb(struct ubus_request *req, int ret) {
    pending_request_t *preq = container_of(req, pending_request_t, req);

    if (!preq->in_use || preq->completed) return;

    /* Mark as completed to prevent timeout callback from firing */
    preq->completed = true;

    /* Cancel timeout timer */
    uloop_timeout_cancel(&preq->timeout);

    /* Map ubus status to HAL status */
    int status;
    switch (ret) {
    case UBUS_STATUS_OK:
        status = UBUS_HAL_STATUS_OK;
        g_consecutive_failures = 0;
        break;
    case UBUS_STATUS_TIMEOUT:
        status = UBUS_HAL_STATUS_TIMEOUT;
        break;
    case UBUS_STATUS_NOT_FOUND:
        status = UBUS_HAL_STATUS_NOT_FOUND;
        /* rpcd may have restarted - invalidate rc_id for lazy reconnect */
        g_rc_id = 0;
        break;
    case UBUS_STATUS_CONNECTION_FAILED:
        status = UBUS_HAL_STATUS_CONN_FAILED;
        g_consecutive_failures++;
        /* Connection lost - invalidate rc_id */
        g_rc_id = 0;
        break;
    case UBUS_STATUS_INVALID_ARGUMENT:
        status = UBUS_HAL_STATUS_ERROR;
        /* Object may have changed - invalidate rc_id */
        g_rc_id = 0;
        break;
    default:
        status = UBUS_HAL_STATUS_ERROR;
        break;
    }

    /* Invoke user callback */
    if (preq->cb) {
        preq->cb(preq->service, preq->installed, preq->running, status, preq->priv);
    }

    /* Release slot */
    preq->in_use = false;
}

/*
 * Connection management with backoff
 */
static int get_backoff_delay(void) {
    if (g_consecutive_failures == 0) return 0;
    int delay = BACKOFF_BASE_SEC << (g_consecutive_failures - 1);
    return (delay > BACKOFF_MAX_SEC) ? BACKOFF_MAX_SEC : delay;
}

/*
 * Abort all pending requests and invoke callbacks with error status.
 * Called before reset_connection to prevent dangling requests.
 */
static void abort_all_pending(int status) {
    for (int i = 0; i < MAX_PENDING_REQUESTS; i++) {
        pending_request_t *preq = &g_pending[i];
        if (preq->in_use && !preq->completed) {
            preq->completed = true;
            uloop_timeout_cancel(&preq->timeout);
            if (g_ctx) {
                ubus_abort_request(g_ctx, &preq->req);
            }
            if (preq->cb) {
                preq->cb(preq->service, false, false, status, preq->priv);
            }
            preq->in_use = false;
        }
    }
}

static void reset_connection(void) {
    /* Abort pending requests before freeing context */
    abort_all_pending(UBUS_HAL_STATUS_CONN_FAILED);

    if (g_ctx) {
        ubus_free(g_ctx);
        g_ctx = NULL;
    }
    g_rc_id = 0;
}

static int ensure_context(void) {
    if (g_ctx) return 0;

    /* Check backoff delay */
    if (g_consecutive_failures > 0) {
        time_t now = time(NULL);
        int delay = get_backoff_delay();
        if (now - g_last_attempt_time < delay) {
            return -1;
        }
    }

    g_last_attempt_time = time(NULL);
    g_ctx = ubus_connect(NULL);
    if (!g_ctx) {
        g_rc_id = 0;
        g_consecutive_failures++;
        return -1;
    }

    /* Register with uloop */
    ubus_add_uloop(g_ctx);

    g_consecutive_failures = 0;
    return 0;
}

static int ensure_rc_id(void) {
    if (ensure_context() < 0) return -1;
    if (g_rc_id != 0) return 0;

    int ret = ubus_lookup_id(g_ctx, "rc", &g_rc_id);
    if (ret) {
        reset_connection();
        return -1;
    }
    return 0;
}

/*
 * HAL implementation
 */
static int real_init(void) {
    if (g_initialized) return 0;

    memset(g_pending, 0, sizeof(g_pending));

    /* Initial connection attempt (non-blocking if fails) */
    ensure_context();

    g_initialized = true;
    return 0;
}

static void real_cleanup(void) {
    if (!g_initialized) return;

    /* Abort pending and reset connection (callbacks not invoked during shutdown) */
    for (int i = 0; i < MAX_PENDING_REQUESTS; i++) {
        if (g_pending[i].in_use) {
            g_pending[i].completed = true;  /* Prevent late callbacks */
            uloop_timeout_cancel(&g_pending[i].timeout);
            if (g_ctx) {
                ubus_abort_request(g_ctx, &g_pending[i].req);
            }
            g_pending[i].in_use = false;
        }
    }

    if (g_ctx) {
        ubus_free(g_ctx);
        g_ctx = NULL;
    }
    g_rc_id = 0;
    g_initialized = false;
}

static int real_query_service_async(const char *name, ubus_query_cb cb, void *priv) {
    if (!g_initialized || !name || !cb) return -1;

    if (ensure_rc_id() < 0) {
        /* Connection failed - invoke callback immediately with error */
        cb(name, false, false, UBUS_HAL_STATUS_CONN_FAILED, priv);
        return 0;  /* Request "initiated" - callback will be called */
    }

    pending_request_t *preq = alloc_request();
    if (!preq) {
        cb(name, false, false, UBUS_HAL_STATUS_ERROR, priv);
        return 0;
    }

    /* Store request info */
    strncpy(preq->service, name, sizeof(preq->service) - 1);
    preq->service[sizeof(preq->service) - 1] = '\0';
    preq->cb = cb;
    preq->priv = priv;
    preq->installed = false;
    preq->running = false;

    /* Build request */
    struct blob_buf b = {0};
    blob_buf_init(&b, 0);
    blobmsg_add_string(&b, "name", name);

    /* Initiate async request */
    int ret = ubus_invoke_async(g_ctx, g_rc_id, "list", b.head, &preq->req);
    if (ret) {
        blob_buf_free(&b);
        release_request(preq);
        cb(name, false, false, UBUS_HAL_STATUS_ERROR, priv);
        return 0;
    }

    /* Set callbacks */
    preq->req.data_cb = query_data_cb;
    preq->req.complete_cb = query_complete_cb;

    /* Complete request setup - this registers with uloop */
    ubus_complete_request_async(g_ctx, &preq->req);

    /* Start timeout timer */
    uloop_timeout_set(&preq->timeout, DEFAULT_TIMEOUT_MS);

    blob_buf_free(&b);
    return 0;
}

static int real_query_services_async(const char **names, size_t count,
                                      ubus_query_cb cb, void *priv) {
    if (!g_initialized || !names || !cb) return -1;

    /*
     * Simple implementation: individual queries.
     * Optimization: could use single "rc list" without name filter
     * and parse all services from response.
     */
    for (size_t i = 0; i < count; i++) {
        if (names[i]) {
            int ret = real_query_service_async(names[i], cb, priv);
            if (ret < 0) return -1;
        }
    }

    return 0;
}

static const ubus_hal_ops_t real_ops = {
    .init = real_init,
    .cleanup = real_cleanup,
    .query_service_async = real_query_service_async,
    .query_services_async = real_query_services_async,
};

const ubus_hal_ops_t *ubus_hal = &real_ops;

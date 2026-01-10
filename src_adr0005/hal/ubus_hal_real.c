#define _POSIX_C_SOURCE 200809L

#include "hal/ubus_hal.h"

#include <string.h>
#include <time.h>
#include <libubus.h>
#include <libubox/blobmsg.h>

static struct ubus_context *ctx = NULL;
static uint32_t rc_id = 0;

/* Reconnection backoff state */
static int consecutive_failures = 0;
static time_t last_attempt_time = 0;
#define BACKOFF_BASE_SEC     1
#define BACKOFF_MAX_SEC      60
#define BACKOFF_RESET_AFTER  3  /* Reset backoff after N successful calls */
static int consecutive_successes = 0;

/* Response parsing for "rc list" */
enum {
    RC_RUNNING,
    RC_ENABLED,
    __RC_MAX,
};

static const struct blobmsg_policy rc_policy[] = {
    [RC_RUNNING] = { .name = "running", .type = BLOBMSG_TYPE_BOOL },
    [RC_ENABLED] = { .name = "enabled", .type = BLOBMSG_TYPE_BOOL },
};

struct query_ctx {
    bool *installed;
    bool *running;
};

static void rc_list_cb(struct ubus_request *req, int type, struct blob_attr *msg) {
    (void)type;
    struct query_ctx *qctx = req->priv;
    struct blob_attr *svc_attr;
    int rem;

    if (!msg || !qctx) return;

    /* msg: { "service_name": { "running": bool, "enabled": bool } } */
    blobmsg_for_each_attr(svc_attr, msg, rem) {
        if (blobmsg_type(svc_attr) != BLOBMSG_TYPE_TABLE) continue;

        /* Service exists in rc */
        if (qctx->installed) *qctx->installed = true;

        struct blob_attr *tb[__RC_MAX];
        blobmsg_parse(rc_policy, __RC_MAX, tb,
                      blobmsg_data(svc_attr), blobmsg_data_len(svc_attr));

        if (tb[RC_RUNNING] && blobmsg_get_bool(tb[RC_RUNNING])) {
            if (qctx->running) *qctx->running = true;
        }
        return; /* Only one service expected per query */
    }
}

static void reset_connection(void) {
    if (ctx) {
        ubus_free(ctx);
        ctx = NULL;
    }
    rc_id = 0;
}

static int get_backoff_delay(void) {
    if (consecutive_failures == 0) return 0;
    int delay = BACKOFF_BASE_SEC << (consecutive_failures - 1);  /* 1, 2, 4, 8, ... */
    return (delay > BACKOFF_MAX_SEC) ? BACKOFF_MAX_SEC : delay;
}

static int ensure_context(void) {
    if (ctx) return 0;

    /* Check backoff delay */
    if (consecutive_failures > 0) {
        time_t now = time(NULL);
        int delay = get_backoff_delay();
        if (now - last_attempt_time < delay) {
            return -1;  /* Still in backoff period */
        }
    }

    last_attempt_time = time(NULL);
    ctx = ubus_connect(NULL);
    if (!ctx) {
        rc_id = 0;
        consecutive_failures++;
        consecutive_successes = 0;
        return -1;
    }

    /* Connection successful */
    consecutive_failures = 0;
    return 0;
}

static int ensure_rc_id(void) {
    if (ensure_context() < 0) return -1;
    if (rc_id != 0) return 0;

    int ret = ubus_lookup_id(ctx, "rc", &rc_id);
    if (ret) {
        reset_connection();
        return -1;
    }
    return 0;
}

/* Check if error indicates connection failure that warrants retry */
static bool is_connection_error(int ret) {
    return ret == UBUS_STATUS_CONNECTION_FAILED ||
           ret == UBUS_STATUS_NO_DATA ||
           ret == UBUS_STATUS_TIMEOUT;
}

static int query_service(const char *name, bool *installed, bool *running) {
    struct blob_buf b = {0};
    struct query_ctx qctx;
    int ret;
    int retries = 1;

    if (!name || !name[0]) return -1;

    if (installed) *installed = false;
    if (running) *running = false;

retry:
    if (ensure_rc_id() < 0) return -1;

    blob_buf_init(&b, 0);
    blobmsg_add_string(&b, "name", name);

    qctx.installed = installed;
    qctx.running = running;
    ret = ubus_invoke(ctx, rc_id, "list", b.head, rc_list_cb, &qctx, 2000);

    blob_buf_free(&b);

    if (ret && is_connection_error(ret) && retries > 0) {
        retries--;
        reset_connection();
        goto retry;
    }

    return ret ? -1 : 0;
}

static int action_service(const char *name, const char *action) {
    struct blob_buf b = {0};
    int ret;
    int retries = 1;

    if (!name || !name[0] || !action || !action[0]) return -1;

retry:
    if (ensure_rc_id() < 0) return -1;

    blob_buf_init(&b, 0);
    blobmsg_add_string(&b, "name", name);
    blobmsg_add_string(&b, "action", action);

    ret = ubus_invoke(ctx, rc_id, "init", b.head, NULL, NULL, 5000);

    blob_buf_free(&b);

    if (ret && is_connection_error(ret) && retries > 0) {
        retries--;
        reset_connection();
        goto retry;
    }

    return ret ? -1 : 0;
}

static int real_init(void) {
    return ensure_context();
}

static void real_cleanup(void) {
    if (ctx) {
        ubus_free(ctx);
        ctx = NULL;
        rc_id = 0;
    }
}

static int real_invoke(const ubus_task_t *task, ubus_result_t *result) {
    if (!task || !result) return -1;

    memset(result, 0, sizeof(*result));
    strncpy(result->service_name, task->service_name, sizeof(result->service_name) - 1);
    result->action = task->action;
    result->request_id = task->request_id;

    int ret = 0;

    switch (task->action) {
    case UBUS_ACTION_QUERY:
        ret = query_service(task->service_name, &result->installed, &result->running);
        break;

    case UBUS_ACTION_START:
        ret = action_service(task->service_name, "start");
        break;

    case UBUS_ACTION_STOP:
        ret = action_service(task->service_name, "stop");
        break;

    default:
        ret = -1;
        break;
    }

    result->success = (ret == 0);
    result->error_code = ret;
    return ret;
}

static int real_register_object(void) {
    /* Not implemented - for future use */
    return 0;
}

static void real_unregister_object(void) {
    /* Not implemented - for future use */
}

static const ubus_hal_ops_t real_ops = {
    .init = real_init,
    .cleanup = real_cleanup,
    .invoke = real_invoke,
    .register_object = real_register_object,
    .unregister_object = real_unregister_object
};

const ubus_hal_ops_t *ubus_hal = &real_ops;

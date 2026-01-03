#include "ubus_service.h"
#include <libubus.h>
#include <libubox/blobmsg.h>

static struct ubus_context *ctx;
static uint32_t service_id;

static int ensure_service_id(void) {
    int ret;

    if (!ctx) {
        ctx = ubus_connect(NULL);
        if (!ctx) {
            service_id = 0;
            return -1;
        }
    }

    if (service_id != 0) return 0;

    ret = ubus_lookup_id(ctx, "service", &service_id);
    if (ret) {
        ubus_free(ctx);
        ctx = NULL;
        service_id = 0;
        return -1;
    }

    return 0;
}

int ubus_service_init(void) {
    return ensure_service_id();
}

void ubus_service_cleanup(void) {
    if (ctx) {
        ubus_free(ctx);
        ctx = NULL;
        service_id = 0;
    }
}

// rc list response: { "service_name": { "running": bool, "enabled": bool } }
enum {
    RC_RUNNING,
    RC_ENABLED,
    __RC_MAX,
};

static const struct blobmsg_policy rc_policy[] = {
    [RC_RUNNING] = { .name = "running", .type = BLOBMSG_TYPE_BOOL },
    [RC_ENABLED] = { .name = "enabled", .type = BLOBMSG_TYPE_BOOL },
};

struct status_ctx {
    bool *installed;
    bool *running;
};

static void rc_list_cb(struct ubus_request *req, int type, struct blob_attr *msg) {
    struct status_ctx *sctx = req->priv;
    struct blob_attr *svc_attr;
    int rem;

    if (!msg || !sctx) return;

    // msg structure: { "service_name": { "running": bool, "enabled": bool } }
    blobmsg_for_each_attr(svc_attr, msg, rem) {
        if (blobmsg_type(svc_attr) != BLOBMSG_TYPE_TABLE) continue;

        // Service exists in rc (init.d script installed)
        if (sctx->installed) *sctx->installed = true;

        // Parse running status
        struct blob_attr *tb[__RC_MAX];
        blobmsg_parse(rc_policy, __RC_MAX, tb,
                      blobmsg_data(svc_attr), blobmsg_data_len(svc_attr));

        if (tb[RC_RUNNING] && blobmsg_get_bool(tb[RC_RUNNING])) {
            if (sctx->running) *sctx->running = true;
        }
        return; // Only one service expected
    }
}

static uint32_t rc_id = 0;

static int ensure_rc_id(void) {
    if (!ctx) {
        ctx = ubus_connect(NULL);
        if (!ctx) {
            rc_id = 0;
            return -1;
        }
    }
    if (rc_id != 0) return 0;

    int ret = ubus_lookup_id(ctx, "rc", &rc_id);
    if (ret) {
        ubus_free(ctx);
        ctx = NULL;
        rc_id = 0;
        return -1;
    }
    return 0;
}

int ubus_service_status(const char *name, bool *installed, bool *running) {
    struct blob_buf b = {0};
    struct status_ctx sctx;
    int ret;

    if (!name || !name[0]) return -1;

    if (installed) *installed = false;
    if (running) *running = false;

    if (ensure_rc_id() < 0) return -1;

    blob_buf_init(&b, 0);
    blobmsg_add_string(&b, "name", name);

    sctx.installed = installed;
    sctx.running = running;
    ret = ubus_invoke(ctx, rc_id, "list", b.head, rc_list_cb, &sctx, 2000);

    blob_buf_free(&b);
    return ret ? -1 : 0;
}

int ubus_service_running(const char *name, bool *running) {
    return ubus_service_status(name, NULL, running);
}

int ubus_service_action(const char *name, const char *action) {
    struct blob_buf b = {0};
    int ret;

    if (!name || !name[0] || !action || !action[0]) return -1;

    if (ensure_rc_id() < 0) return -1;

    blob_buf_init(&b, 0);
    blobmsg_add_string(&b, "name", name);
    blobmsg_add_string(&b, "action", action);

    ret = ubus_invoke(ctx, rc_id, "init", b.head, NULL, NULL, 5000);

    blob_buf_free(&b);
    return ret ? -1 : 0;
}

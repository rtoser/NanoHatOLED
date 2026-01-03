/*
 * Simple ubus test program
 * Verifies libubus linking works and can list services
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libubus.h>
#include <libubox/blobmsg_json.h>

static struct ubus_context *ctx;
static struct blob_buf b;

static void receive_list_result(struct ubus_request *req, int type, struct blob_attr *msg)
{
    char *str;
    if (!msg)
        return;

    str = blobmsg_format_json_indent(msg, true, 0);
    if (str) {
        printf("Services:\n%s\n", str);
        free(str);
    }
}

int main(int argc, char **argv)
{
    uint32_t id;
    int ret;

    ctx = ubus_connect(NULL);
    if (!ctx) {
        fprintf(stderr, "Failed to connect to ubus\n");
        return 1;
    }

    printf("Connected to ubus\n");

    ret = ubus_lookup_id(ctx, "service", &id);
    if (ret) {
        fprintf(stderr, "Failed to lookup 'service' object: %s\n", ubus_strerror(ret));
        ubus_free(ctx);
        return 1;
    }

    printf("Found 'service' object (id=%u)\n", id);

    blob_buf_init(&b, 0);
    ret = ubus_invoke(ctx, id, "list", b.head, receive_list_result, NULL, 3000);
    if (ret) {
        fprintf(stderr, "Failed to invoke 'list': %s\n", ubus_strerror(ret));
    }

    blob_buf_free(&b);
    ubus_free(ctx);

    return ret ? 1 : 0;
}

/*
 * sys_status mock for host testing
 */
#include "../../src/sys_status.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct sys_status_ctx {
    int dummy;
};

static sys_status_ctx_t g_mock_ctx;

sys_status_ctx_t *sys_status_init(task_queue_t *tq, result_queue_t *rq) {
    (void)tq;
    (void)rq;
    memset(&g_mock_ctx, 0, sizeof(g_mock_ctx));
    return &g_mock_ctx;
}

void sys_status_cleanup(sys_status_ctx_t *ctx) {
    (void)ctx;
}

void sys_status_update_local(sys_status_ctx_t *ctx, sys_status_t *status) {
    (void)ctx;
    if (status) {
        /* Provide some mock data */
        status->cpu_usage = 25.0f;
        status->cpu_temp = 45.0f;
        status->mem_total_kb = 512 * 1024;
        status->mem_available_kb = 256 * 1024;
        status->uptime_sec = 3600;
        strncpy(status->hostname, "TestHost", sizeof(status->hostname) - 1);
        strncpy(status->ip_addr, "192.168.1.1", sizeof(status->ip_addr) - 1);
        status->rx_bytes = 1024 * 1024;
        status->tx_bytes = 512 * 1024;
        status->rx_speed = 1024;
        status->tx_speed = 512;
    }
}

void sys_status_request_services(sys_status_ctx_t *ctx, sys_status_t *status) {
    (void)ctx;
    (void)status;
}

int sys_status_process_results(sys_status_ctx_t *ctx, sys_status_t *status) {
    (void)ctx;
    (void)status;
    return 0;
}

void sys_status_format_uptime(uint32_t uptime_sec, char *buf, size_t buflen) {
    if (!buf || buflen == 0) return;

    uint32_t days = uptime_sec / 86400;
    uint32_t hours = (uptime_sec % 86400) / 3600;
    uint32_t mins = (uptime_sec % 3600) / 60;

    if (days > 0) {
        snprintf(buf, buflen, "%ud %uh", days, hours);
    } else {
        snprintf(buf, buflen, "%uh %um", hours, mins);
    }
}

void sys_status_format_bytes(uint64_t bytes, char *buf, size_t buflen) {
    if (!buf || buflen == 0) return;

    const char *units[] = {"B", "K", "M", "G", "T"};
    int unit = 0;
    double size = (double)bytes;

    while (size >= 1024 && unit < 4) {
        size /= 1024;
        unit++;
    }

    if (unit == 0) {
        snprintf(buf, buflen, "%lu%s", (unsigned long)bytes, units[unit]);
    } else {
        snprintf(buf, buflen, "%.1f%s", size, units[unit]);
    }
}

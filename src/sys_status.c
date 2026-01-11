#define _POSIX_C_SOURCE 200809L

#include "sys_status.h"
#include "hal/ubus_hal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <inttypes.h>
#include <sys/sysinfo.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>

/* Request ID counter for matching responses */
static uint32_t g_next_request_id = 1;

struct sys_status_ctx {
    /* For CPU usage calculation */
    uint64_t prev_idle;
    uint64_t prev_total;

    /* For network speed calculation */
    uint64_t prev_rx_bytes;
    uint64_t prev_tx_bytes;
    uint64_t prev_net_time_ms;  /* Use milliseconds for accurate speed calculation */
    char cached_gw_iface[16];   /* Cached gateway interface name */
    time_t gw_cache_time;       /* When gateway was last checked (seconds) */

    /* Cached file handles */
    FILE *fp_stat;
    FILE *fp_net;
    FILE *fp_mem;
    FILE *fp_temp;
};

static void safe_copy(char *dst, size_t dst_size, const char *src) {
    if (!dst || dst_size == 0) return;
    if (!src) {
        dst[0] = '\0';
        return;
    }
    snprintf(dst, dst_size, "%s", src);
}

sys_status_ctx_t *sys_status_init(void) {
    sys_status_ctx_t *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) return NULL;

    /* Open /proc files once */
    ctx->fp_stat = fopen("/proc/stat", "r");
    ctx->fp_net = fopen("/proc/net/dev", "r");
    ctx->fp_mem = fopen("/proc/meminfo", "r");
    ctx->fp_temp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");

    return ctx;
}

void sys_status_cleanup(sys_status_ctx_t *ctx) {
    if (!ctx) return;

    if (ctx->fp_stat) fclose(ctx->fp_stat);
    if (ctx->fp_net) fclose(ctx->fp_net);
    if (ctx->fp_mem) fclose(ctx->fp_mem);
    if (ctx->fp_temp) fclose(ctx->fp_temp);

    free(ctx);
}

static void update_cpu_usage(sys_status_ctx_t *ctx, sys_status_t *status) {
    if (!ctx->fp_stat) return;

    rewind(ctx->fp_stat);
    char line[256];
    if (fgets(line, sizeof(line), ctx->fp_stat)) {
        uint64_t user, nice, system, idle, iowait, irq, softirq;
        if (sscanf(line, "cpu %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64 " %" SCNu64,
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq) == 7) {
            uint64_t total = user + nice + system + idle + iowait + irq + softirq;
            uint64_t idle_all = idle + iowait;

            if (ctx->prev_total > 0) {
                uint64_t total_diff = total - ctx->prev_total;
                uint64_t idle_diff = idle_all - ctx->prev_idle;
                if (total_diff > 0) {
                    status->cpu_usage = 100.0f * (1.0f - (float)idle_diff / (float)total_diff);
                }
            }
            ctx->prev_total = total;
            ctx->prev_idle = idle_all;
        }
    }
}

static void update_cpu_temp(sys_status_ctx_t *ctx, sys_status_t *status) {
    if (!ctx->fp_temp) return;

    rewind(ctx->fp_temp);
    int temp;
    if (fscanf(ctx->fp_temp, "%d", &temp) == 1) {
        status->cpu_temp = temp / 1000.0f;
    }
}

static void update_memory(sys_status_ctx_t *ctx, sys_status_t *status) {
    if (!ctx->fp_mem) return;

    rewind(ctx->fp_mem);
    char line[128];
    int found = 0;
    while (fgets(line, sizeof(line), ctx->fp_mem) && found < 2) {
        if (strncmp(line, "MemTotal:", 9) == 0) {
            sscanf(line + 9, "%" SCNu64, &status->mem_total_kb);
            found++;
        } else if (strncmp(line, "MemAvailable:", 13) == 0) {
            sscanf(line + 13, "%" SCNu64, &status->mem_available_kb);
            found++;
        }
    }
}

static void update_hostname(sys_status_t *status) {
    if (gethostname(status->hostname, sizeof(status->hostname) - 1) == 0) {
        status->hostname[sizeof(status->hostname) - 1] = '\0';
    } else {
        safe_copy(status->hostname, sizeof(status->hostname), "Unknown");
    }
}

static void update_uptime(sys_status_t *status) {
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        status->uptime_sec = (uint32_t)si.uptime;
    }
}

static void update_ip_addr(sys_status_t *status) {
    struct ifaddrs *ifaddr, *ifa;
    status->ip_addr[0] = '\0';

    if (getifaddrs(&ifaddr) != 0) {
        safe_copy(status->ip_addr, sizeof(status->ip_addr), "No IP");
        return;
    }

    /* Priority: br-lan > eth0 > wlan0 > any non-loopback */
    const char *priority[] = {"br-lan", "eth0", "wlan0", NULL};

    for (int p = 0; priority[p] && status->ip_addr[0] == '\0'; p++) {
        for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (strcmp(ifa->ifa_name, priority[p]) == 0) {
                struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                inet_ntop(AF_INET, &addr->sin_addr, status->ip_addr, sizeof(status->ip_addr));
                break;
            }
        }
    }

    /* Fallback: first non-loopback */
    if (status->ip_addr[0] == '\0') {
        for (ifa = ifaddr; ifa; ifa = ifa->ifa_next) {
            if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_INET) continue;
            if (ifa->ifa_flags & IFF_LOOPBACK) continue;
            struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
            inet_ntop(AF_INET, &addr->sin_addr, status->ip_addr, sizeof(status->ip_addr));
            break;
        }
    }

    freeifaddrs(ifaddr);

    if (status->ip_addr[0] == '\0') {
        safe_copy(status->ip_addr, sizeof(status->ip_addr), "No IP");
    }
}

/* Get current time in milliseconds using monotonic clock */
static uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
}

static void update_network_stats(sys_status_ctx_t *ctx, sys_status_t *status) {
    if (!ctx->fp_net) return;

    uint64_t now_ms = get_time_ms();
    time_t now_sec = time(NULL);

    /* Refresh gateway interface and IP every 30 seconds (or on first call) */
    if (ctx->cached_gw_iface[0] == '\0' || (now_sec - ctx->gw_cache_time) >= 30) {
        ctx->cached_gw_iface[0] = '\0';
        status->gateway[0] = '\0';

        FILE *fp_route = fopen("/proc/net/route", "r");
        if (fp_route) {
            char line[256];
            (void)fgets(line, sizeof(line), fp_route);  /* skip header */
            while (fgets(line, sizeof(line), fp_route)) {
                char iface[16];
                unsigned int dest, gateway;
                if (sscanf(line, "%15s %x %x", iface, &dest, &gateway) == 3 && dest == 0) {
                    safe_copy(ctx->cached_gw_iface, sizeof(ctx->cached_gw_iface), iface);
                    /* Convert gateway hex to IP string (little-endian bytes) */
                    if (gateway != 0) {
                        snprintf(status->gateway, sizeof(status->gateway),
                                 "%u.%u.%u.%u",
                                 gateway & 0xFF,
                                 (gateway >> 8) & 0xFF,
                                 (gateway >> 16) & 0xFF,
                                 (gateway >> 24) & 0xFF);
                    }
                    break;
                }
            }
            fclose(fp_route);
        }

        if (ctx->cached_gw_iface[0] == '\0') {
            safe_copy(ctx->cached_gw_iface, sizeof(ctx->cached_gw_iface), "eth0");
        }
        if (status->gateway[0] == '\0') {
            safe_copy(status->gateway, sizeof(status->gateway), "--");
        }
        ctx->gw_cache_time = now_sec;
    }

    /* Get traffic for gateway interface */
    rewind(ctx->fp_net);
    char line[256];
    while (fgets(line, sizeof(line), ctx->fp_net)) {
        char *colon = strchr(line, ':');
        if (!colon) continue;
        *colon = ' ';

        char iface[16];
        unsigned long rx, tx;
        char *p = line;
        while (*p == ' ') p++;

        if (sscanf(p, "%15s %lu %*d %*d %*d %*d %*d %*d %*d %lu", iface, &rx, &tx) >= 3) {
            if (strcmp(iface, ctx->cached_gw_iface) == 0) {
                status->rx_bytes = (uint64_t)rx;
                status->tx_bytes = (uint64_t)tx;
                break;
            }
        }
    }

    /* Calculate speed using millisecond precision */
    if (ctx->prev_net_time_ms > 0 && now_ms > ctx->prev_net_time_ms) {
        uint64_t elapsed_ms = now_ms - ctx->prev_net_time_ms;
        /* Calculate bytes per second: (delta_bytes * 1000) / elapsed_ms */
        if (elapsed_ms > 0 && status->rx_bytes >= ctx->prev_rx_bytes) {
            status->rx_speed = (status->rx_bytes - ctx->prev_rx_bytes) * 1000 / elapsed_ms;
        }
        if (elapsed_ms > 0 && status->tx_bytes >= ctx->prev_tx_bytes) {
            status->tx_speed = (status->tx_bytes - ctx->prev_tx_bytes) * 1000 / elapsed_ms;
        }
    }
    ctx->prev_rx_bytes = status->rx_bytes;
    ctx->prev_tx_bytes = status->tx_bytes;
    ctx->prev_net_time_ms = now_ms;
}

void sys_status_update_local(sys_status_ctx_t *ctx, sys_status_t *status) {
    if (!ctx || !status) return;

    /* Initialize service list once (Phase 3 placeholder) */
    if (status->service_count == 0) {
        const service_config_t *cfg = service_config_get();
        if (cfg && cfg->count > 0) {
            for (size_t i = 0; i < cfg->count && i < MAX_SERVICES; i++) {
                safe_copy(status->services[i].name, sizeof(status->services[i].name),
                          cfg->services[i].name);
                status->services[i].installed = false;
                status->services[i].running = false;
                status->services[i].query_pending = false;
                status->services[i].status_valid = false;
                status->services[i].request_id = 0;
                status->services[i].request_time_ms = 0;
                status->services[i].last_update_ms = 0;
            }
            status->service_count = cfg->count;
        }
    }

    update_cpu_usage(ctx, status);
    update_cpu_temp(ctx, status);
    update_memory(ctx, status);
    update_hostname(status);
    update_uptime(status);
    update_ip_addr(status);
    update_network_stats(ctx, status);
}

void sys_status_format_uptime(uint32_t uptime_sec, char *buf, size_t buflen) {
    if (!buf || buflen == 0) return;

    uint32_t days = uptime_sec / 86400;
    uint32_t hours = (uptime_sec % 86400) / 3600;
    uint32_t mins = (uptime_sec % 3600) / 60;

    if (days > 0) {
        snprintf(buf, buflen, "%ud %uh %um", days, hours, mins);
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

void sys_status_format_speed_bps(uint64_t bytes_per_sec, char *buf, size_t buflen) {
    if (!buf || buflen == 0) return;

    /* Convert bytes to bits */
    uint64_t bits_per_sec = bytes_per_sec * 8;

    if (bits_per_sec >= 1000000) {
        snprintf(buf, buflen, "%.1fMb/s", (double)bits_per_sec / 1000000.0);
    } else if (bits_per_sec >= 1000) {
        snprintf(buf, buflen, "%.1fKb/s", (double)bits_per_sec / 1000.0);
    } else {
        snprintf(buf, buflen, "%lub/s", (unsigned long)bits_per_sec);
    }
}

/*
 * Callback context for service query
 */
typedef struct {
    sys_status_t *status;
    uint32_t request_id;
} query_ctx_t;

/*
 * Callback invoked when ubus query completes
 */
static void service_query_cb(const char *service, bool installed,
                              bool running, int status_code, void *priv) {
    query_ctx_t *qctx = (query_ctx_t *)priv;
    if (!qctx || !qctx->status) {
        free(qctx);
        return;
    }

    sys_status_t *status = qctx->status;
    uint64_t now_ms = get_time_ms();

    /* Find matching service */
    for (size_t i = 0; i < status->service_count; i++) {
        if (strcmp(status->services[i].name, service) != 0) continue;

        /* Check request ID to avoid stale response overwriting newer state */
        if (status->services[i].request_id != qctx->request_id) {
            /* Stale response - ignore */
            break;
        }

        /* Update service status */
        status->services[i].query_pending = false;
        status->services[i].last_update_ms = now_ms;

        if (status_code == UBUS_HAL_STATUS_OK) {
            status->services[i].installed = installed;
            status->services[i].running = running;
            status->services[i].status_valid = true;
        } else {
            /* Query failed - mark invalid but keep last known state */
            status->services[i].status_valid = false;
        }
        break;
    }

    free(qctx);
}

int sys_status_query_services(sys_status_ctx_t *ctx, sys_status_t *status) {
    (void)ctx;  /* Not used currently */

    if (!status || !ubus_hal) return 0;

    uint64_t now_ms = get_time_ms();
    int queries_sent = 0;

    for (size_t i = 0; i < status->service_count; i++) {
        service_status_t *svc = &status->services[i];

        /* Skip if query already pending */
        if (svc->query_pending) continue;

        /* Skip if recently updated */
        if (svc->last_update_ms > 0 &&
            (now_ms - svc->last_update_ms) < SERVICE_REFRESH_INTERVAL_MS) {
            continue;
        }
        /* Allocate callback context */
        query_ctx_t *qctx = malloc(sizeof(query_ctx_t));
        if (!qctx) continue;

        /* Assign new request ID */
        uint32_t req_id = g_next_request_id++;
        if (g_next_request_id == 0) g_next_request_id = 1;  /* Avoid 0 */

        qctx->status = status;
        qctx->request_id = req_id;

        /* Mark as pending */
        svc->query_pending = true;
        svc->request_id = req_id;
        svc->request_time_ms = now_ms;

        /* Initiate async query */
        int ret = ubus_hal->query_service_async(svc->name, service_query_cb, qctx);
        if (ret < 0) {
            /* Immediate failure */
            svc->query_pending = false;
            free(qctx);
        } else {
            queries_sent++;
        }
    }

    return queries_sent;
}

bool sys_status_has_pending_queries(const sys_status_t *status) {
    if (!status) return false;

    for (size_t i = 0; i < status->service_count; i++) {
        if (status->services[i].query_pending) {
            return true;
        }
    }
    return false;
}

/*
 * Callback context for service control
 */
typedef struct {
    sys_status_t *status;
    int index;
    bool start;
    sys_status_control_cb cb;
    void *priv;
} control_ctx_t;

/*
 * Callback invoked when ubus control completes
 */
static void service_control_cb(const char *service, bool success,
                                int status_code, void *priv) {
    (void)service;
    (void)status_code;

    control_ctx_t *cctx = (control_ctx_t *)priv;
    if (!cctx || !cctx->status) {
        free(cctx);
        return;
    }

    sys_status_t *status = cctx->status;
    int idx = cctx->index;

    if (idx >= 0 && (size_t)idx < status->service_count) {
        /* Force a query refresh to get updated status */
        status->services[idx].last_update_ms = 0;

        /* If successful, optimistically update running state */
        if (success) {
            status->services[idx].running = cctx->start;
        }
    }

    if (cctx->cb) {
        cctx->cb(idx, success, status_code, cctx->priv);
    }

    free(cctx);
}

int sys_status_control_service(sys_status_ctx_t *ctx, sys_status_t *status,
                                int index, bool start,
                                sys_status_control_cb cb, void *priv) {
    (void)ctx;

    if (!status || !ubus_hal || !ubus_hal->control_service_async) return -1;
    if (index < 0 || (size_t)index >= status->service_count) return -1;

    service_status_t *svc = &status->services[index];

    /* Allocate callback context */
    control_ctx_t *cctx = malloc(sizeof(control_ctx_t));
    if (!cctx) return -1;

    cctx->status = status;
    cctx->index = index;
    cctx->start = start;
    cctx->cb = cb;
    cctx->priv = priv;

    /* Initiate async control */
    int ret = ubus_hal->control_service_async(svc->name, start, service_control_cb, cctx);
    if (ret < 0) {
        free(cctx);
        return -1;
    }

    return 0;
}

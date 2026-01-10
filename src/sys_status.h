/*
 * System status module for NanoHat OLED
 *
 * Provides local system info from /proc (Phase 3).
 * Service status fields are reserved for Phase 4 ubus integration.
 */
#ifndef SYS_STATUS_H
#define SYS_STATUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "service_config.h"
#define MAX_SERVICES MAX_MONITORED_SERVICES
#define HOSTNAME_MAX_LEN 32
#define IP_ADDR_MAX_LEN  16

typedef struct {
    char name[SERVICE_NAME_MAX_LEN];
    bool installed;
    bool running;
    bool query_pending;      /* Reserved for Phase 4 */
    bool status_valid;       /* Reserved for Phase 4 */
    uint32_t request_id;     /* Reserved for Phase 4 */
    uint64_t request_time_ms; /* Reserved for Phase 4 */
    uint64_t last_update_ms;  /* Reserved for Phase 4 */
} service_status_t;

typedef struct sys_status {
    /* System info (from /proc, synchronous) */
    float cpu_usage;
    float cpu_temp;
    uint64_t mem_total_kb;
    uint64_t mem_available_kb;
    uint32_t uptime_sec;
    char hostname[HOSTNAME_MAX_LEN];
    char ip_addr[IP_ADDR_MAX_LEN];
    char gateway[IP_ADDR_MAX_LEN];

    /* Network stats (WAN interface) */
    uint64_t rx_bytes;
    uint64_t tx_bytes;
    uint64_t rx_speed;    /* bytes/sec */
    uint64_t tx_speed;    /* bytes/sec */

    /* Service status (Phase 4 via ubus) */
    service_status_t services[MAX_SERVICES];
    size_t service_count;
} sys_status_t;

typedef struct sys_status_ctx sys_status_ctx_t;

/*
 * Initialize sys_status context.
 */
sys_status_ctx_t *sys_status_init(void);

/*
 * Cleanup and free context.
 */
void sys_status_cleanup(sys_status_ctx_t *ctx);

/*
 * Update local system info (CPU, memory, etc.) from /proc.
 * This is synchronous and fast.
 */
void sys_status_update_local(sys_status_ctx_t *ctx, sys_status_t *status);

/*
 * Utility: format uptime as "Xd Xh Xm" or "Xh Xm"
 */
void sys_status_format_uptime(uint32_t uptime_sec, char *buf, size_t buflen);

/*
 * Utility: format bytes as "X.XK", "X.XM", etc.
 */
void sys_status_format_bytes(uint64_t bytes, char *buf, size_t buflen);

/*
 * Utility: format speed (bytes/sec) as "X.XMb/s", "X.XKb/s", etc.
 */
void sys_status_format_speed_bps(uint64_t bytes_per_sec, char *buf, size_t buflen);

#endif

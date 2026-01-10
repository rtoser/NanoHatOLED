/*
 * System status module for NanoHat OLED
 *
 * Provides async service status queries via task/result queues,
 * plus local system info from /proc.
 */
#ifndef SYS_STATUS_H
#define SYS_STATUS_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "service_config.h"
#include "task_queue.h"
#include "result_queue.h"

#define MAX_SERVICES MAX_MONITORED_SERVICES
#define HOSTNAME_MAX_LEN 32
#define IP_ADDR_MAX_LEN  16

#define SERVICE_QUERY_TIMEOUT_MS 5000

typedef struct {
    char name[SERVICE_NAME_MAX_LEN];
    bool installed;
    bool running;
    bool query_pending;      /* Request sent, waiting for response */
    bool status_valid;       /* Last query succeeded, status is fresh */
    uint32_t request_id;
    uint64_t request_time_ms; /* When query was sent (for timeout) */
    uint64_t last_update_ms;  /* When status was last successfully updated */
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

    /* Service status (async via ubus) */
    service_status_t services[MAX_SERVICES];
    size_t service_count;
} sys_status_t;

typedef struct sys_status_ctx sys_status_ctx_t;

/*
 * Initialize sys_status context.
 * task_queue: for sending service query requests
 * result_queue: for receiving service query responses
 */
sys_status_ctx_t *sys_status_init(task_queue_t *tq, result_queue_t *rq);

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
 * Send async service status query requests.
 * Call this periodically to refresh service status.
 */
void sys_status_request_services(sys_status_ctx_t *ctx, sys_status_t *status);

/*
 * Process any pending service query results.
 * Call this in the main loop after poll wakes up.
 * Returns number of results processed.
 */
int sys_status_process_results(sys_status_ctx_t *ctx, sys_status_t *status);

/*
 * Utility: format uptime as "Xd Xh" or "Xh Xm"
 */
void sys_status_format_uptime(uint32_t uptime_sec, char *buf, size_t buflen);

/*
 * Utility: format bytes as "X.XK", "X.XM", etc.
 */
void sys_status_format_bytes(uint64_t bytes, char *buf, size_t buflen);

#endif

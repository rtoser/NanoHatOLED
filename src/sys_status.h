#ifndef SYS_STATUS_H
#define SYS_STATUS_H

#include <stdint.h>
#include <stdbool.h>

// Service status structure
typedef struct {
    char name[16];          // Service name
    bool running;           // Service running status
} service_status_t;

#define MAX_SERVICES 4

typedef struct {
    float cpu_usage;        // CPU usage percentage
    float cpu_temp;         // CPU temperature in Celsius
    uint64_t mem_total;     // Total memory in KB
    uint64_t mem_free;      // Free memory in KB
    uint64_t mem_available; // Available memory in KB
    char ip_addr[16];       // Primary IP address
    char hostname[32];      // Hostname
    uint32_t uptime;        // Uptime in seconds
    uint64_t rx_bytes;      // Network RX bytes (total)
    uint64_t tx_bytes;      // Network TX bytes (total)
    uint64_t rx_speed;      // Network RX speed (bytes/sec)
    uint64_t tx_speed;      // Network TX speed (bytes/sec)
    // Services
    service_status_t services[MAX_SERVICES];
    int service_count;
} sys_status_t;

// Get current system status
void sys_status_update(sys_status_t *status);

// Format uptime as string (e.g., "2d 5h 30m")
void sys_status_format_uptime(uint32_t uptime, char *buf, int buflen);

// Format bytes as human readable (e.g., "1.5 GB")
void sys_status_format_bytes(uint64_t bytes, char *buf, int buflen);

#endif

#include "sys_status.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <time.h>
#include <sys/sysinfo.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>
#include "ubus_service.h"

// Cached file handles to avoid repeated open/close overhead
static FILE *fp_stat = NULL;
static FILE *fp_net = NULL;
static FILE *fp_mem = NULL;
static FILE *fp_temp = NULL;

static uint64_t prev_idle = 0;
static uint64_t prev_total = 0;
static uint64_t prev_rx_bytes = 0;
static uint64_t prev_tx_bytes = 0;
static time_t prev_net_time = 0;

typedef struct {
    const char *display_name;
    const char *ubus_name;
} service_entry_t;

static const service_entry_t services[] = {
    {"xray_core", "xray_core"},
    {"dropbear", "dropbear"},
    {"uhttpd", "uhttpd"},
    {"dockerd", "dockerd"},
    {NULL, NULL}
};

void sys_status_init(void) {
    // Open files once
    fp_stat = fopen("/proc/stat", "r");
    fp_net = fopen("/proc/net/dev", "r");
    fp_mem = fopen("/proc/meminfo", "r");
    fp_temp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
}

void sys_status_cleanup(void) {
    if (fp_stat) fclose(fp_stat);
    if (fp_net) fclose(fp_net);
    if (fp_mem) fclose(fp_mem);
    if (fp_temp) fclose(fp_temp);
}

void sys_status_update_basic(sys_status_t *status) {
    status->cpu_usage = 0.0f;
    status->cpu_temp = 0.0f;
    status->mem_total = 0;
    status->mem_free = 0;
    status->mem_available = 0;
    status->ip_addr[0] = '\0';
    status->hostname[0] = '\0';
    status->uptime = 0;
    status->rx_bytes = 0;
    status->tx_bytes = 0;
    status->rx_speed = 0;
    status->tx_speed = 0;

    // 1. CPU usage
    if (fp_stat) {
        rewind(fp_stat);
        char line[256];
        if (fgets(line, sizeof(line), fp_stat)) {
            uint64_t user, nice, system, idle, iowait, irq, softirq;
            if (sscanf(line, "cpu %lu %lu %lu %lu %lu %lu %lu",
                       &user, &nice, &system, &idle, &iowait, &irq, &softirq) == 7) {
                uint64_t total = user + nice + system + idle + iowait + irq + softirq;
                uint64_t idle_all = idle + iowait;

                if (prev_total > 0) {
                    uint64_t total_diff = total - prev_total;
                    uint64_t idle_diff = idle_all - prev_idle;
                    if (total_diff > 0) {
                        status->cpu_usage = 100.0f * (1.0f - (float)idle_diff / total_diff);
                    }
                }
                prev_total = total;
                prev_idle = idle_all;
            }
        }
    }

    // 2. CPU temperature
    if (fp_temp) {
        rewind(fp_temp);
        int temp;
        if (fscanf(fp_temp, "%d", &temp) == 1) {
            status->cpu_temp = temp / 1000.0f;
        }
    }

    // 3. Memory info
    if (fp_mem) {
        rewind(fp_mem);
        char line[128];
        int found = 0;
        while (fgets(line, sizeof(line), fp_mem) && found < 3) {
            if (strncmp(line, "MemTotal:", 9) == 0) {
                sscanf(line + 9, "%lu", &status->mem_total);
                found++;
            } else if (strncmp(line, "MemFree:", 8) == 0) {
                sscanf(line + 8, "%lu", &status->mem_free);
                found++;
            } else if (strncmp(line, "MemAvailable:", 13) == 0) {
                sscanf(line + 13, "%lu", &status->mem_available);
                found++;
            }
        }
    }

    // 4. Hostname (ensure NUL termination)
    if (gethostname(status->hostname, sizeof(status->hostname) - 1) == 0) {
        status->hostname[sizeof(status->hostname) - 1] = '\0';
    } else {
        strncpy(status->hostname, "Unknown", sizeof(status->hostname) - 1);
        status->hostname[sizeof(status->hostname) - 1] = '\0';
    }

    // 5. Uptime
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        status->uptime = si.uptime;
    }

    // 6. IP address (Prioritize br-lan, then eth0, then anything non-local)
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == 0) {
        // Priority list
        const char *iface_priority[] = {"br-lan", "eth0", "wlan0", NULL};
        
        for (int p = 0; iface_priority[p] != NULL && status->ip_addr[0] == 0; p++) {
            for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
                if (ifa->ifa_addr == NULL) continue;
                if (ifa->ifa_addr->sa_family != AF_INET) continue;
                
                if (strcmp(ifa->ifa_name, iface_priority[p]) == 0) {
                    struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                    inet_ntop(AF_INET, &addr->sin_addr, status->ip_addr, sizeof(status->ip_addr));
                    break;
                }
            }
        }
        
        // Fallback: take first non-loopback
        if (status->ip_addr[0] == 0) {
            for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
                if (ifa->ifa_addr == NULL) continue;
                if (ifa->ifa_addr->sa_family != AF_INET) continue;
                if (ifa->ifa_flags & IFF_LOOPBACK) continue;
                
                struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                inet_ntop(AF_INET, &addr->sin_addr, status->ip_addr, sizeof(status->ip_addr));
                break;
            }
        }
        freeifaddrs(ifaddr);
    }
    if (status->ip_addr[0] == 0) strcpy(status->ip_addr, "No IP");

    // 7. Network stats - get traffic from default gateway interface
    // First, find the default route interface from /proc/net/route
    char gw_iface[16] = "";
    FILE *fp_route = fopen("/proc/net/route", "r");
    if (fp_route) {
        char line[256];
        fgets(line, sizeof(line), fp_route); // Skip header
        while (fgets(line, sizeof(line), fp_route)) {
            char iface[16];
            unsigned int dest;
            if (sscanf(line, "%15s %x", iface, &dest) == 2) {
                if (dest == 0) { // Default route (destination 0.0.0.0)
                    strncpy(gw_iface, iface, sizeof(gw_iface) - 1);
                    break;
                }
            }
        }
        fclose(fp_route);
    }

    // Fallback to IP interface priority if no default route found
    if (gw_iface[0] == '\0') {
        const char *fallback[] = {"br-lan", "eth0", "wlan0", NULL};
        for (int i = 0; fallback[i]; i++) {
            strncpy(gw_iface, fallback[i], sizeof(gw_iface) - 1);
            break;
        }
    }

    // Now get traffic for the gateway interface
    if (fp_net && gw_iface[0] != '\0') {
        rewind(fp_net);
        char line[256];

        while (fgets(line, sizeof(line), fp_net)) {
            char *colon = strchr(line, ':');
            if (!colon) continue;
            *colon = ' ';

            char iface[16];
            uint64_t rx, tx;
            char *p = line;
            while (*p == ' ') p++;

            if (sscanf(p, "%15s %lu %*d %*d %*d %*d %*d %*d %*d %lu", iface, &rx, &tx) >= 3) {
                if (strcmp(iface, gw_iface) == 0) {
                    status->rx_bytes = rx;
                    status->tx_bytes = tx;
                    break;
                }
            }
        }
    }

    // Calculate network speed
    time_t now = time(NULL);
    if (prev_net_time > 0 && now > prev_net_time) {
        time_t elapsed = now - prev_net_time;
        status->rx_speed = (status->rx_bytes > prev_rx_bytes) ? (status->rx_bytes - prev_rx_bytes) / elapsed : 0;
        status->tx_speed = (status->tx_bytes > prev_tx_bytes) ? (status->tx_bytes - prev_tx_bytes) / elapsed : 0;
    }
    prev_rx_bytes = status->rx_bytes;
    prev_tx_bytes = status->tx_bytes;
    prev_net_time = now;
}

void sys_status_update_services(sys_status_t *status) {
    status->service_count = 0;

    for (int i = 0; services[i].display_name != NULL && status->service_count < MAX_SERVICES; i++) {
        bool installed = false;
        bool running = false;

        // Check if service is installed and running
        if (ubus_service_status(services[i].ubus_name, &installed, &running) < 0) {
            continue; // ubus error, skip
        }

        // Only add installed services to the list
        if (!installed) {
            continue;
        }

        service_status_t *svc = &status->services[status->service_count];
        strncpy(svc->name, services[i].display_name, sizeof(svc->name) - 1);
        svc->name[sizeof(svc->name) - 1] = '\0';
        strncpy(svc->ubus_name, services[i].ubus_name, sizeof(svc->ubus_name) - 1);
        svc->ubus_name[sizeof(svc->ubus_name) - 1] = '\0';
        svc->running = running;
        status->service_count++;
    }
}

void sys_status_format_uptime(uint32_t uptime, char *buf, int buflen) {
    uint32_t days = uptime / 86400;
    uint32_t hours = (uptime % 86400) / 3600;
    uint32_t mins = (uptime % 3600) / 60;

    if (days > 0) {
        snprintf(buf, buflen, "%ud %uh", days, hours);
    } else {
        snprintf(buf, buflen, "%uh %um", hours, mins);
    }
}

void sys_status_format_bytes(uint64_t bytes, char *buf, int buflen) {
    const char *units[] = {"B", "K", "M", "G", "T"};
    int unit = 0;
    double size = bytes;

    while (size >= 1024 && unit < 4) {
        size /= 1024;
        unit++;
    }
    
    if (unit == 0) snprintf(buf, buflen, "%lu%s", bytes, units[unit]);
    else snprintf(buf, buflen, "%.1f%s", size, units[unit]);
}

#include "sys_status.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <ctype.h>
#include <dirent.h>
#include <sys/sysinfo.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/if.h>

static uint64_t prev_idle = 0;
static uint64_t prev_total = 0;
static uint64_t prev_rx_bytes = 0;
static uint64_t prev_tx_bytes = 0;
static time_t prev_net_time = 0;
static time_t prev_service_time = 0;

#define SERVICE_CHECK_INTERVAL 5  // seconds

// Check if a process with given name is running (without fork)
static bool process_running(const char *name) {
    DIR *dir = opendir("/proc");
    if (!dir) return false;

    struct dirent *ent;
    bool found = false;

    while ((ent = readdir(dir)) != NULL) {
        // Only check numeric directories (PIDs)
        if (ent->d_type != DT_DIR || !isdigit(ent->d_name[0]))
            continue;

        char path[280], comm[32];
        snprintf(path, sizeof(path), "/proc/%s/comm", ent->d_name);

        FILE *f = fopen(path, "r");
        if (f) {
            if (fgets(comm, sizeof(comm), f)) {
                comm[strcspn(comm, "\n")] = 0;  // Remove newline
                if (strcmp(comm, name) == 0) {
                    found = true;
                    fclose(f);
                    break;
                }
            }
            fclose(f);
        }
    }
    closedir(dir);
    return found;
}

void sys_status_update(sys_status_t *status) {
    memset(status, 0, sizeof(sys_status_t));

    // CPU usage
    FILE *fp = fopen("/proc/stat", "r");
    if (fp) {
        char line[256];
        if (fgets(line, sizeof(line), fp)) {
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
        fclose(fp);
    }

    // CPU temperature
    fp = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
    if (fp) {
        int temp;
        if (fscanf(fp, "%d", &temp) == 1) {
            status->cpu_temp = temp / 1000.0f;
        }
        fclose(fp);
    }

    // Memory info
    fp = fopen("/proc/meminfo", "r");
    if (fp) {
        char line[128];
        while (fgets(line, sizeof(line), fp)) {
            if (strncmp(line, "MemTotal:", 9) == 0) {
                sscanf(line + 9, "%lu", &status->mem_total);
            } else if (strncmp(line, "MemFree:", 8) == 0) {
                sscanf(line + 8, "%lu", &status->mem_free);
            } else if (strncmp(line, "MemAvailable:", 13) == 0) {
                sscanf(line + 13, "%lu", &status->mem_available);
            }
        }
        fclose(fp);
    }

    // Hostname
    gethostname(status->hostname, sizeof(status->hostname) - 1);

    // Uptime
    struct sysinfo si;
    if (sysinfo(&si) == 0) {
        status->uptime = si.uptime;
    }

    // IP address and network stats
    struct ifaddrs *ifaddr, *ifa;
    if (getifaddrs(&ifaddr) == 0) {
        for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == NULL) continue;

            // Skip loopback
            if (ifa->ifa_flags & IFF_LOOPBACK) continue;

            // Get IPv4 address
            if (ifa->ifa_addr->sa_family == AF_INET && status->ip_addr[0] == 0) {
                struct sockaddr_in *addr = (struct sockaddr_in *)ifa->ifa_addr;
                inet_ntop(AF_INET, &addr->sin_addr, status->ip_addr, sizeof(status->ip_addr));
            }
        }
        freeifaddrs(ifaddr);
    }

    // Network stats (WAN interface: br-lan.10)
    fp = fopen("/proc/net/dev", "r");
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (strstr(line, "br-lan.10")) {
                char iface[16];
                sscanf(line, "%s %lu %*d %*d %*d %*d %*d %*d %*d %lu",
                       iface, &status->rx_bytes, &status->tx_bytes);
                break;
            }
        }
        fclose(fp);
    }

    // Calculate network speed
    time_t now = time(NULL);
    if (prev_net_time > 0 && now > prev_net_time) {
        time_t elapsed = now - prev_net_time;
        status->rx_speed = (status->rx_bytes - prev_rx_bytes) / elapsed;
        status->tx_speed = (status->tx_bytes - prev_tx_bytes) / elapsed;
    }
    prev_rx_bytes = status->rx_bytes;
    prev_tx_bytes = status->tx_bytes;
    prev_net_time = now;

    // Fallback IP if none found
    if (status->ip_addr[0] == 0) {
        strcpy(status->ip_addr, "No IP");
    }

    // Service status checks - only update every SERVICE_CHECK_INTERVAL seconds
    // Process name to check (from /proc/PID/comm)
    static const char *services[] = {"xray", "dropbear", "dockerd", NULL};
    static service_status_t cached_services[MAX_SERVICES];
    static int cached_count = 0;

    time_t svc_now = time(NULL);
    if (prev_service_time == 0 || svc_now - prev_service_time >= SERVICE_CHECK_INTERVAL) {
        prev_service_time = svc_now;
        cached_count = 0;

        for (int i = 0; services[i] != NULL && cached_count < MAX_SERVICES; i++) {
            strncpy(cached_services[cached_count].name, services[i],
                    sizeof(cached_services[0].name) - 1);
            cached_services[cached_count].running = process_running(services[i]);
            cached_count++;
        }
    }

    // Copy cached results to status
    memcpy(status->services, cached_services, sizeof(cached_services));
    status->service_count = cached_count;
}

void sys_status_format_uptime(uint32_t uptime, char *buf, int buflen) {
    uint32_t days = uptime / 86400;
    uint32_t hours = (uptime % 86400) / 3600;
    uint32_t mins = (uptime % 3600) / 60;

    if (days > 0) {
        snprintf(buf, buflen, "%ud %uh %um", days, hours, mins);
    } else if (hours > 0) {
        snprintf(buf, buflen, "%uh %um", hours, mins);
    } else {
        snprintf(buf, buflen, "%um", mins);
    }
}

void sys_status_format_bytes(uint64_t bytes, char *buf, int buflen) {
    const char *units[] = {"B", "KB", "MB", "GB", "TB"};
    int unit = 0;
    double size = bytes;

    while (size >= 1024 && unit < 4) {
        size /= 1024;
        unit++;
    }

    if (unit == 0) {
        snprintf(buf, buflen, "%lu %s", bytes, units[unit]);
    } else {
        snprintf(buf, buflen, "%.1f %s", size, units[unit]);
    }
}

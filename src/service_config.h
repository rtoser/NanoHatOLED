/*
 * Service configuration for NanoHat OLED
 *
 * Services are configured at compile time via MONITORED_SERVICES macro.
 * Example: make MONITORED_SERVICES="xray_core,dropbear,uhttpd"
 */
#ifndef SERVICE_CONFIG_H
#define SERVICE_CONFIG_H

#include <stddef.h>

#define MAX_MONITORED_SERVICES 8
#define SERVICE_NAME_MAX_LEN   32

#ifndef MONITORED_SERVICES
#define MONITORED_SERVICES "xray_core,collectd,luci_statistics,dropbear,uhttpd"
#endif

typedef struct {
    char name[SERVICE_NAME_MAX_LEN];
} service_entry_t;

typedef struct {
    service_entry_t services[MAX_MONITORED_SERVICES];
    size_t count;
} service_config_t;

/*
 * Initialize service config by parsing MONITORED_SERVICES string.
 * Call once at startup.
 */
void service_config_init(service_config_t *config);

/*
 * Get the global service configuration.
 * Returns NULL if not initialized.
 */
const service_config_t *service_config_get(void);

#endif

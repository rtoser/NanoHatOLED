#include "service_config.h"
#include <string.h>

static service_config_t g_config;
static int g_initialized = 0;

void service_config_init(service_config_t *config) {
    if (!config) {
        config = &g_config;
    }

    memset(config, 0, sizeof(*config));

    const char *services_str = MONITORED_SERVICES;
    if (!services_str || !services_str[0]) {
        g_initialized = 1;
        return;
    }

    char buf[256];
    strncpy(buf, services_str, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';

    char *saveptr = NULL;
    char *token = strtok_r(buf, ",", &saveptr);

    while (token && config->count < MAX_MONITORED_SERVICES) {
        /* Skip leading whitespace */
        while (*token == ' ' || *token == '\t') token++;

        /* Skip empty tokens */
        if (*token == '\0') {
            token = strtok_r(NULL, ",", &saveptr);
            continue;
        }

        /* Remove trailing whitespace */
        size_t len = strlen(token);
        while (len > 0 && (token[len - 1] == ' ' || token[len - 1] == '\t')) {
            token[--len] = '\0';
        }

        if (len > 0 && len < SERVICE_NAME_MAX_LEN) {
            strncpy(config->services[config->count].name, token, SERVICE_NAME_MAX_LEN - 1);
            config->services[config->count].name[SERVICE_NAME_MAX_LEN - 1] = '\0';
            config->count++;
        }

        token = strtok_r(NULL, ",", &saveptr);
    }

    if (config == &g_config) {
        g_initialized = 1;
    }
}

const service_config_t *service_config_get(void) {
    if (!g_initialized) {
        service_config_init(&g_config);
    }
    return &g_config;
}

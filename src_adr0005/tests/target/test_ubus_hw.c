/*
 * Target hardware test for ubus integration
 *
 * Tests real ubus service queries on OpenWrt device.
 * Run on target: ./test_ubus_hw
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "hal/ubus_hal.h"
#include "service_config.h"

static int test_count = 0;
static int pass_count = 0;

#define TEST(name) do { \
    test_count++; \
    printf("TEST: %s ... ", name); \
} while(0)

#define PASS() do { \
    pass_count++; \
    printf("PASS\n"); \
} while(0)

#define FAIL(msg) do { \
    printf("FAIL: %s\n", msg); \
} while(0)

static void test_ubus_init(void) {
    TEST("ubus_hal init");

    if (!ubus_hal || !ubus_hal->init) {
        FAIL("ubus_hal not available");
        return;
    }

    int ret = ubus_hal->init();
    if (ret == 0) {
        PASS();
    } else {
        FAIL("init returned non-zero");
    }
}

static void test_query_service(const char *service_name) {
    char test_name[64];
    snprintf(test_name, sizeof(test_name), "query service '%s'", service_name);
    TEST(test_name);

    if (!ubus_hal || !ubus_hal->invoke) {
        FAIL("ubus_hal->invoke not available");
        return;
    }

    ubus_task_t task = {0};
    strncpy(task.service_name, service_name, sizeof(task.service_name) - 1);
    task.action = UBUS_ACTION_QUERY;
    task.request_id = 1;
    task.timeout_ms = 2000;

    ubus_result_t result = {0};
    int ret = ubus_hal->invoke(&task, &result);

    if (ret == 0 && result.success) {
        printf("PASS (installed=%d, running=%d)\n", result.installed, result.running);
        pass_count++;
    } else {
        printf("FAIL (ret=%d, success=%d, error=%d)\n", ret, result.success, result.error_code);
    }
}

static void test_service_config(void) {
    TEST("service_config parsing");

    service_config_t config;
    service_config_init(&config);

    printf("PASS (count=%zu, services=[", config.count);
    for (size_t i = 0; i < config.count; i++) {
        printf("%s%s", config.services[i].name, i < config.count - 1 ? "," : "");
    }
    printf("])\n");
    pass_count++;
}

static void test_configured_services(void) {
    const service_config_t *cfg = service_config_get();
    if (!cfg || cfg->count == 0) {
        printf("INFO: No services configured, skipping\n");
        return;
    }

    for (size_t i = 0; i < cfg->count; i++) {
        test_query_service(cfg->services[i].name);
    }
}

static void test_ubus_cleanup(void) {
    TEST("ubus_hal cleanup");

    if (ubus_hal && ubus_hal->cleanup) {
        ubus_hal->cleanup();
        PASS();
    } else {
        FAIL("ubus_hal->cleanup not available");
    }
}

int main(void) {
    printf("=== UBUS Hardware Test ===\n");
    printf("MONITORED_SERVICES: %s\n\n", MONITORED_SERVICES);

    test_ubus_init();
    test_service_config();
    test_configured_services();

    /* Also test some common services */
    printf("\n--- Common Services ---\n");
    test_query_service("dropbear");
    test_query_service("uhttpd");
    test_query_service("procd");

    test_ubus_cleanup();

    printf("\n=== Results: %d/%d passed ===\n", pass_count, test_count);

    return (pass_count == test_count) ? 0 : 1;
}

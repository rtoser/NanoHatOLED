#include <stdio.h>
#include <poll.h>

#include "hal/gpio_hal.h"
#include "mocks/gpio_mock.h"
#include "mocks/time_mock.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

static uint64_t ms_to_ns(uint64_t ms) {
    return ms * 1000000ULL;
}

static void setup_default_lines(bool debounce_supported) {
    gpio_mock_set_debounce_supported(debounce_supported);
    gpio_mock_set_line_value(0, 1);
    gpio_mock_set_line_value(1, 1);
    gpio_mock_set_line_value(2, 1);
    gpio_mock_clear_events();
}

static int test_short_press_k1(void) {
    setup_default_lines(true);
    TEST_ASSERT(gpio_hal->init() == 0);

    time_mock_set_now_ms(0);
    gpio_mock_inject_edge(0, EDGE_FALLING, ms_to_ns(0));
    time_mock_set_now_ms(100);
    gpio_mock_inject_edge(0, EDGE_RISING, ms_to_ns(100));

    gpio_event_t evt;
    int ret = gpio_hal->wait_event(200, &evt);
    TEST_ASSERT(ret == 1);
    TEST_ASSERT(evt.type == GPIO_EVT_BTN_K1_SHORT);
    gpio_hal->cleanup();
    return 0;
}

static int test_short_press_k2(void) {
    setup_default_lines(true);
    TEST_ASSERT(gpio_hal->init() == 0);

    time_mock_set_now_ms(0);
    gpio_mock_inject_edge(1, EDGE_FALLING, ms_to_ns(0));
    time_mock_set_now_ms(100);
    gpio_mock_inject_edge(1, EDGE_RISING, ms_to_ns(100));

    gpio_event_t evt;
    int ret = gpio_hal->wait_event(200, &evt);
    TEST_ASSERT(ret == 1);
    TEST_ASSERT(evt.type == GPIO_EVT_BTN_K2_SHORT);
    gpio_hal->cleanup();
    return 0;
}

static int test_short_press_k3(void) {
    setup_default_lines(true);
    TEST_ASSERT(gpio_hal->init() == 0);

    time_mock_set_now_ms(0);
    gpio_mock_inject_edge(2, EDGE_FALLING, ms_to_ns(0));
    time_mock_set_now_ms(100);
    gpio_mock_inject_edge(2, EDGE_RISING, ms_to_ns(100));

    gpio_event_t evt;
    int ret = gpio_hal->wait_event(200, &evt);
    TEST_ASSERT(ret == 1);
    TEST_ASSERT(evt.type == GPIO_EVT_BTN_K3_SHORT);
    gpio_hal->cleanup();
    return 0;
}

static int test_long_press_k2(void) {
    setup_default_lines(true);
    TEST_ASSERT(gpio_hal->init() == 0);

    time_mock_set_now_ms(0);
    gpio_mock_inject_edge(1, EDGE_FALLING, ms_to_ns(0));
    time_mock_set_now_ms(600);
    gpio_mock_inject_edge(1, EDGE_RISING, ms_to_ns(600));

    gpio_event_t evt;
    int ret = gpio_hal->wait_event(10, &evt);
    TEST_ASSERT(ret == 1);
    TEST_ASSERT(evt.type == GPIO_EVT_BTN_K2_LONG);
    gpio_hal->cleanup();
    return 0;
}

static int test_debounce_soft_fallback(void) {
    setup_default_lines(false);
    TEST_ASSERT(gpio_hal->init() == 0);

    time_mock_set_now_ms(0);
    gpio_mock_inject_edge(0, EDGE_FALLING, ms_to_ns(0));
    time_mock_set_now_ms(10);
    gpio_mock_inject_edge(0, EDGE_RISING, ms_to_ns(10));  // bounce
    time_mock_set_now_ms(100);
    gpio_mock_inject_edge(0, EDGE_RISING, ms_to_ns(100));

    gpio_event_t evt;
    int ret = gpio_hal->wait_event(200, &evt);
    TEST_ASSERT(ret == 1);
    TEST_ASSERT(evt.type == GPIO_EVT_BTN_K1_SHORT);
    gpio_hal->cleanup();
    return 0;
}

static int test_timeout_no_event(void) {
    setup_default_lines(true);
    TEST_ASSERT(gpio_hal->init() == 0);

    time_mock_set_now_ms(0);
    gpio_event_t evt;
    int ret = gpio_hal->wait_event(50, &evt);
    TEST_ASSERT(ret == 0);
    gpio_hal->cleanup();
    return 0;
}

static int test_gpio_fd_wakeup(void) {
    setup_default_lines(true);
    TEST_ASSERT(gpio_hal->init() == 0);

    int fd = gpio_hal->get_fd();
    TEST_ASSERT(fd >= 0);

    struct pollfd pfd = {.fd = fd, .events = POLLIN};
    time_mock_set_now_ms(0);
    gpio_mock_inject_edge(2, EDGE_FALLING, ms_to_ns(0));
    // 注入释放边沿，确保生成短按事件。
    time_mock_set_now_ms(50);
    gpio_mock_inject_edge(2, EDGE_RISING, ms_to_ns(50));

    int pret = poll(&pfd, 1, 100);
    TEST_ASSERT(pret == 1);

    gpio_event_t evt;
    int ret = gpio_hal->wait_event(100, &evt);
    TEST_ASSERT(ret == 1);
    TEST_ASSERT(evt.type == GPIO_EVT_BTN_K3_SHORT);
    gpio_hal->cleanup();
    return 0;
}

static int test_reinit_after_cleanup(void) {
    setup_default_lines(true);
    TEST_ASSERT(gpio_hal->init() == 0);
    gpio_hal->cleanup();

    setup_default_lines(true);
    TEST_ASSERT(gpio_hal->init() == 0);
    gpio_hal->cleanup();
    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_short_press_k1();
    rc |= test_short_press_k2();
    rc |= test_short_press_k3();
    rc |= test_long_press_k2();
    rc |= test_debounce_soft_fallback();
    rc |= test_timeout_no_event();
    rc |= test_gpio_fd_wakeup();
    rc |= test_reinit_after_cleanup();

    if (rc == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return rc;
}

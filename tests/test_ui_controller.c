#include <stdio.h>
#include <string.h>

#include "ui_controller.h"
#include "mocks/display_mock.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

static int test_render_on_event(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);
    display_mock_reset();

    app_event_t evt = {
        .type = EVT_BTN_K1_SHORT,
        .line = 0,
        .timestamp_ns = 0,
        .data = 0
    };

    ui_controller_handle_event(&ui, &evt);
    TEST_ASSERT(ui_controller_render(&ui) == true);
    TEST_ASSERT(display_mock_begin_count() == 1);
    TEST_ASSERT(display_mock_end_count() == 1);
    TEST_ASSERT(display_mock_draw_count() >= 1);

    const char *last = display_mock_last_text();
    TEST_ASSERT(last != NULL && strlen(last) > 0);

    return 0;
}

static int test_tick_updates_text(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);
    display_mock_reset();

    app_event_t tick = {
        .type = EVT_TICK,
        .line = 0,
        .timestamp_ns = 0,
        .data = 2
    };

    ui_controller_handle_event(&ui, &tick);
    ui_controller_render(&ui);

    const char *last = display_mock_last_text();
    TEST_ASSERT(last != NULL);
    TEST_ASSERT(strncmp(last, "TICK:", 5) == 0);

    return 0;
}

static int test_wake_on_keypress(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);
    ui.power_on = false;

    app_event_t evt = {
        .type = EVT_BTN_K1_SHORT,
        .line = 0,
        .timestamp_ns = 123,
        .data = 0
    };

    ui_controller_handle_event(&ui, &evt);
    TEST_ASSERT(ui.power_on == true);
    TEST_ASSERT(ui.last_input_ns == 123);

    return 0;
}

static int test_k2_toggle_off(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    app_event_t evt = {
        .type = EVT_BTN_K2_SHORT,
        .line = 1,
        .timestamp_ns = 500,
        .data = 0
    };

    ui_controller_handle_event(&ui, &evt);
    TEST_ASSERT(ui.power_on == false);
    TEST_ASSERT(ui.last_input_ns == 500);

    return 0;
}

static int test_k2_toggle_on(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);
    ui.power_on = false;

    app_event_t evt = {
        .type = EVT_BTN_K2_SHORT,
        .line = 1,
        .timestamp_ns = 600,
        .data = 0
    };

    ui_controller_handle_event(&ui, &evt);
    TEST_ASSERT(ui.power_on == true);
    TEST_ASSERT(ui.last_input_ns == 600);

    return 0;
}

static int test_idle_exact_boundary(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);
    ui.idle_timeout_ms = 100;

    app_event_t press = {
        .type = EVT_BTN_K1_SHORT,
        .line = 0,
        .timestamp_ns = 1000,
        .data = 0
    };
    ui_controller_handle_event(&ui, &press);
    TEST_ASSERT(ui.power_on == true);

    app_event_t tick = {
        .type = EVT_TICK,
        .line = 0,
        .timestamp_ns = 1000 + (uint64_t)ui.idle_timeout_ms * 1000000ULL,
        .data = 1
    };
    ui_controller_handle_event(&ui, &tick);
    TEST_ASSERT(ui.power_on == false);

    return 0;
}

static int test_idle_init_from_tick(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);
    ui.idle_timeout_ms = 100;

    app_event_t tick = {
        .type = EVT_TICK,
        .line = 0,
        .timestamp_ns = 2000,
        .data = 1
    };
    ui_controller_handle_event(&ui, &tick);
    TEST_ASSERT(ui.last_input_ns == 2000);
    TEST_ASSERT(ui.power_on == true);

    return 0;
}

static int test_auto_sleep(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);
    ui.idle_timeout_ms = 100;

    app_event_t press = {
        .type = EVT_BTN_K1_SHORT,
        .line = 0,
        .timestamp_ns = 1000,
        .data = 0
    };
    ui_controller_handle_event(&ui, &press);
    TEST_ASSERT(ui.power_on == true);

    app_event_t tick = {
        .type = EVT_TICK,
        .line = 0,
        .timestamp_ns = 1000 + (uint64_t)ui.idle_timeout_ms * 1000000ULL + 1,
        .data = 1
    };
    ui_controller_handle_event(&ui, &tick);
    TEST_ASSERT(ui.power_on == false);

    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_render_on_event();
    rc |= test_tick_updates_text();
    rc |= test_wake_on_keypress();
    rc |= test_k2_toggle_off();
    rc |= test_k2_toggle_on();
    rc |= test_idle_exact_boundary();
    rc |= test_idle_init_from_tick();
    rc |= test_auto_sleep();

    if (rc == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return rc;
}

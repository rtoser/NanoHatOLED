/*
 * UI Controller tests
 *
 * Tests for the integrated page controller with multi-page UI.
 */
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

static int test_init_power_on(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    TEST_ASSERT(ui.power_on == true);
    TEST_ASSERT(page_controller_is_screen_on(&ui.page_ctrl) == true);

    return 0;
}

static int test_render_calls_u8g2(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    /* Trigger a render */
    TEST_ASSERT(ui_controller_render(&ui) == true);

    /* Second render without change should return false (needs_render cleared) */
    TEST_ASSERT(ui_controller_render(&ui) == false);

    return 0;
}

static int test_k2_short_home_turns_off(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    /* On home page (page 0), K2 short press should turn off screen */
    app_event_t evt = {
        .type = EVT_BTN_K2_SHORT,
        .line = 1,
        .timestamp_ns = 1000000,  /* 1ms */
        .data = 0
    };

    ui_controller_handle_event(&ui, &evt);
    TEST_ASSERT(ui.power_on == false);
    TEST_ASSERT(page_controller_is_screen_on(&ui.page_ctrl) == false);

    return 0;
}

static int test_any_key_wakes_screen(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    /* Turn off screen first */
    ui.page_ctrl.screen_state = SCREEN_OFF;
    ui.power_on = false;

    /* K1 should wake screen */
    app_event_t evt = {
        .type = EVT_BTN_K1_SHORT,
        .line = 0,
        .timestamp_ns = 2000000,
        .data = 0
    };

    ui_controller_handle_event(&ui, &evt);
    TEST_ASSERT(ui.power_on == true);
    TEST_ASSERT(page_controller_is_screen_on(&ui.page_ctrl) == true);

    return 0;
}

static int test_k3_switches_page(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    TEST_ASSERT(ui.page_ctrl.current_page == 0);

    /* K3 should go to next page */
    app_event_t evt = {
        .type = EVT_BTN_K3_SHORT,
        .line = 2,
        .timestamp_ns = 1000000,
        .data = 0
    };

    ui_controller_handle_event(&ui, &evt);

    /* Page should have changed (animation started) */
    TEST_ASSERT(ui.page_ctrl.current_page == 1);

    return 0;
}

static int test_k1_switches_page(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    /* Start on page 1 */
    ui.page_ctrl.current_page = 1;

    /* K1 should go to previous page */
    app_event_t evt = {
        .type = EVT_BTN_K1_SHORT,
        .line = 0,
        .timestamp_ns = 1000000,
        .data = 0
    };

    ui_controller_handle_event(&ui, &evt);

    TEST_ASSERT(ui.page_ctrl.current_page == 0);

    return 0;
}

static int test_k2_long_enter_mode(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    /* Go to services page (page 2) which supports enter mode */
    ui.page_ctrl.current_page = 2;

    TEST_ASSERT(ui.page_ctrl.page_mode == PAGE_MODE_VIEW);

    /* K2 long press should enter mode */
    app_event_t evt = {
        .type = EVT_BTN_K2_LONG,
        .line = 1,
        .timestamp_ns = 1000000,
        .data = 0
    };

    ui_controller_handle_event(&ui, &evt);

    /* Should be in enter mode now (or animation started) */
    TEST_ASSERT(ui.page_ctrl.page_mode == PAGE_MODE_ENTER ||
                ui.page_ctrl.anim.type == ANIM_ENTER_MODE);

    return 0;
}

static int test_auto_sleep(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    /* Set short idle timeout */
    page_controller_set_idle_timeout(&ui.page_ctrl, 100);

    /* Trigger activity */
    app_event_t press = {
        .type = EVT_BTN_K1_SHORT,
        .line = 0,
        .timestamp_ns = 1000000,  /* 1ms */
        .data = 0
    };
    ui_controller_handle_event(&ui, &press);
    ui.page_ctrl.current_page = 0;  /* Reset page after K1 */
    TEST_ASSERT(ui.power_on == true);

    /* Wait for idle timeout */
    app_event_t tick = {
        .type = EVT_TICK,
        .line = 0,
        .timestamp_ns = 200000000,  /* 200ms, past 100ms timeout */
        .data = 1
    };
    ui_controller_handle_event(&ui, &tick);

    TEST_ASSERT(ui.power_on == false);

    return 0;
}

static int test_shutdown_event(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    app_event_t evt = {
        .type = EVT_SHUTDOWN,
        .line = 0,
        .timestamp_ns = 0,
        .data = 0
    };

    ui_controller_handle_event(&ui, &evt);
    TEST_ASSERT(ui.power_on == false);

    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_init_power_on();
    rc |= test_render_calls_u8g2();
    rc |= test_k2_short_home_turns_off();
    rc |= test_any_key_wakes_screen();
    rc |= test_k3_switches_page();
    rc |= test_k1_switches_page();
    rc |= test_k2_long_enter_mode();
    rc |= test_auto_sleep();
    rc |= test_shutdown_event();

    if (rc == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return rc;
}

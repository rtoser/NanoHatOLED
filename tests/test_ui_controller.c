/*
 * UI controller tests for ADR0006 (single-thread)
 */
#include <stdio.h>

#include "ui_controller.h"
#include "anim.h"
#include "pages/pages.h"

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

static int test_init_power_on(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    ASSERT_TRUE(page_controller_is_screen_on(&ui.page_ctrl));
    ASSERT_TRUE(ui_controller_next_timeout_ms(&ui) == UI_TICK_STATIC_MS);

    ui_controller_cleanup(&ui);
    return 0;
}

static int test_k2_short_turns_off(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    ui_controller_handle_button(&ui, KEY_K2, false, 1000);
    ASSERT_TRUE(!page_controller_is_screen_on(&ui.page_ctrl));
    ASSERT_TRUE(ui_controller_next_timeout_ms(&ui) == UI_TICK_IDLE_MS);

    ui_controller_cleanup(&ui);
    return 0;
}

static int test_any_key_wakes_screen(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    ui.page_ctrl.screen_state = SCREEN_OFF;
    ui.power_on = false;

    ui_controller_handle_button(&ui, KEY_K1, false, 2000);
    ASSERT_TRUE(page_controller_is_screen_on(&ui.page_ctrl));

    ui_controller_cleanup(&ui);
    return 0;
}

static int test_k3_starts_animation(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    ui_controller_handle_button(&ui, KEY_K3, false, 1000);
    ASSERT_TRUE(page_controller_is_animating(&ui.page_ctrl));
    ASSERT_TRUE(ui_controller_next_timeout_ms(&ui) == UI_TICK_ANIM_MS);

    ui_controller_cleanup(&ui);
    return 0;
}

static int test_auto_sleep(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    page_controller_set_idle_timeout(&ui.page_ctrl, 100);
    ui_controller_handle_button(&ui, KEY_K1, false, 1000);

    ui_controller_tick(&ui, 1200);
    ASSERT_TRUE(!page_controller_is_screen_on(&ui.page_ctrl));

    ui_controller_cleanup(&ui);
    return 0;
}

static int test_page_order_and_wrap(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    int page_count = 0;
    const page_t **pages = pages_get_list(&page_count);
    ASSERT_TRUE(page_count == 4);

    uint64_t t = 1000;

    ASSERT_TRUE(ui.page_ctrl.current_page == 0);
    ASSERT_TRUE(ui.page_ctrl.pages[0] == pages[0]);

    ui_controller_handle_button(&ui, KEY_K3, false, t);
    ui_controller_tick(&ui, t + ANIM_SLIDE_DURATION_MS + 1);
    ASSERT_TRUE(ui.page_ctrl.current_page == 1);
    ASSERT_TRUE(ui.page_ctrl.pages[1] == pages[1]);

    t += 500;
    ui_controller_handle_button(&ui, KEY_K3, false, t);
    ui_controller_tick(&ui, t + ANIM_SLIDE_DURATION_MS + 1);
    ASSERT_TRUE(ui.page_ctrl.current_page == 2);
    ASSERT_TRUE(ui.page_ctrl.pages[2] == pages[2]);

    t += 500;
    ui_controller_handle_button(&ui, KEY_K3, false, t);
    ui_controller_tick(&ui, t + ANIM_SLIDE_DURATION_MS + 1);
    ASSERT_TRUE(ui.page_ctrl.current_page == 3);
    ASSERT_TRUE(ui.page_ctrl.pages[3] == pages[3]);

    t += 500;
    ui_controller_handle_button(&ui, KEY_K3, false, t);
    ui_controller_tick(&ui, t + ANIM_SLIDE_DURATION_MS + 1);
    ASSERT_TRUE(ui.page_ctrl.current_page == 0);

    ui_controller_cleanup(&ui);
    return 0;
}

static int test_wake_does_not_change_page(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    ui.page_ctrl.current_page = 2;
    ui.page_ctrl.screen_state = SCREEN_OFF;
    ui.page_ctrl.anim.type = ANIM_NONE;

    ui_controller_handle_button(&ui, KEY_K1, false, 2000);
    ASSERT_TRUE(page_controller_is_screen_on(&ui.page_ctrl));
    ASSERT_TRUE(ui.page_ctrl.current_page == 2);

    ui_controller_cleanup(&ui);
    return 0;
}

static int test_k2_short_turns_off_any_page(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    ui.page_ctrl.current_page = 2;
    ui.page_ctrl.screen_state = SCREEN_ON;
    ui.page_ctrl.anim.type = ANIM_NONE;

    ui_controller_handle_button(&ui, KEY_K2, false, 3000);
    ASSERT_TRUE(!page_controller_is_screen_on(&ui.page_ctrl));
    ASSERT_TRUE(ui.page_ctrl.current_page == 2);

    ui_controller_cleanup(&ui);
    return 0;
}

static int test_k2_long_on_non_enter_shakes(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    ui.page_ctrl.current_page = 0; /* Home: can_enter=false */
    ui.page_ctrl.page_mode = PAGE_MODE_VIEW;
    ui.page_ctrl.anim.type = ANIM_NONE;

    ui_controller_handle_button(&ui, KEY_K2, true, 4000);
    ASSERT_TRUE(ui.page_ctrl.anim.type == ANIM_TITLE_SHAKE);

    ui_controller_cleanup(&ui);
    return 0;
}

int main(void) {
    int failures = 0;

    printf("=== test_ui_controller ===\n");
    failures += test_init_power_on();
    failures += test_k2_short_turns_off();
    failures += test_any_key_wakes_screen();
    failures += test_k3_starts_animation();
    failures += test_auto_sleep();
    failures += test_page_order_and_wrap();
    failures += test_wake_does_not_change_page();
    failures += test_k2_short_turns_off_any_page();
    failures += test_k2_long_on_non_enter_shakes();

    printf("=== failures: %d ===\n", failures);
    return failures ? 1 : 0;
}

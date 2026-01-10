/*
 * UI refresh policy tests (50ms / 1000ms / 0)
 */
#include <stdio.h>

#include "ui_controller.h"

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

static int test_refresh_policy(void) {
    ui_controller_t ui;
    ui_controller_init(&ui);

    /* Static mode */
    ASSERT_TRUE(ui_controller_next_timeout_ms(&ui) == UI_TICK_STATIC_MS);

    /* Animation mode */
    ui_controller_handle_button(&ui, KEY_K3, false, 1000);
    ASSERT_TRUE(ui_controller_next_timeout_ms(&ui) == UI_TICK_ANIM_MS);

    /* Screen off */
    ui_controller_handle_button(&ui, KEY_K2, false, 2000);
    ASSERT_TRUE(ui_controller_next_timeout_ms(&ui) == UI_TICK_IDLE_MS);

    ui_controller_cleanup(&ui);
    return 0;
}

int main(void) {
    printf("=== test_ui_refresh_policy ===\n");
    int failures = test_refresh_policy();
    printf("=== failures: %d ===\n", failures);
    return failures ? 1 : 0;
}

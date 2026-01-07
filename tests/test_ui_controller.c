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

int main(void) {
    int rc = 0;
    rc |= test_render_on_event();
    rc |= test_tick_updates_text();

    if (rc == 0) {
        printf("ALL TESTS PASSED\n");
    }
    return rc;
}

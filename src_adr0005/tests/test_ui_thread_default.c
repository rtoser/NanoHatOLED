/*
 * UI Thread default handler tests
 *
 * Tests the UI thread with the default page controller integration.
 */
#include <stdio.h>
#include <unistd.h>

#include "ui_thread.h"
#include "mocks/display_mock.h"

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT FAILED: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

int main(void) {
    event_queue_t q;
    ui_thread_t ui;
    app_event_t evt = {0};

    TEST_ASSERT(event_queue_init(&q, 8) == 0);
    TEST_ASSERT(ui_thread_start_default(&ui, &q, NULL) == 0);

    /* Small delay for thread to start */
    usleep(5000);

    /* Send a K1 event - this should switch pages */
    evt = (app_event_t){
        .type = EVT_BTN_K1_SHORT,
        .line = 0,
        .timestamp_ns = 1000000,  /* 1ms */
        .data = 0
    };
    TEST_ASSERT(event_queue_push(&q, &evt) == EQ_RESULT_OK);

    /* Give thread time to process */
    usleep(20000);

    /* Screen should be on */
    TEST_ASSERT(ui.controller.power_on == true);
    TEST_ASSERT(page_controller_is_screen_on(&ui.controller.page_ctrl) == true);

    /* Page should have changed (K1 goes to previous, wraps to last) */
    TEST_ASSERT(ui.controller.page_ctrl.current_page == 2);

    /* Send K2 short press on non-home page - should do nothing to power */
    evt = (app_event_t){
        .type = EVT_BTN_K2_SHORT,
        .line = 1,
        .timestamp_ns = 2000000,
        .data = 0
    };
    TEST_ASSERT(event_queue_push(&q, &evt) == EQ_RESULT_OK);
    usleep(10000);

    /* Screen should still be on (K2 short only turns off on home page) */
    TEST_ASSERT(ui.controller.power_on == true);

    /* Shutdown */
    app_event_t shutdown = {
        .type = EVT_SHUTDOWN,
        .line = 0,
        .timestamp_ns = 0,
        .data = 0
    };
    event_queue_push(&q, &shutdown);

    ui_thread_stop(&ui);
    event_queue_destroy(&q);

    printf("ALL TESTS PASSED\n");
    return 0;
}

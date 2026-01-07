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

    TEST_ASSERT(event_queue_init(&q, 8) == 0);
    TEST_ASSERT(ui_thread_start_default(&ui, &q, NULL) == 0);

    display_mock_reset();

    app_event_t evt = {
        .type = EVT_BTN_K1_SHORT,
        .line = 0,
        .timestamp_ns = 0,
        .data = 0
    };
    TEST_ASSERT(event_queue_push(&q, &evt) == EQ_RESULT_OK);

    usleep(10000);
    TEST_ASSERT(display_mock_begin_count() > 0);

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

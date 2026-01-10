#ifndef UI_CONTROLLER_H
#define UI_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "event_queue.h"
#include "page_controller.h"
#include "sys_status.h"

typedef struct {
    bool power_on;
    app_event_type_t last_event;
    uint64_t tick_count;
    bool needs_render;
    uint64_t last_input_ns;
    uint32_t idle_timeout_ms;
    uint64_t last_event_ms;  /* Real timestamp for animations */

    /* Page controller integration */
    page_controller_t page_ctrl;
    sys_status_t *sys_status;
    sys_status_ctx_t *sys_status_ctx;
} ui_controller_t;

void ui_controller_init(ui_controller_t *ui);
void ui_controller_handle_event(ui_controller_t *ui, const app_event_t *event);
bool ui_controller_render(ui_controller_t *ui);

/* Set system status context for data updates */
void ui_controller_set_status(ui_controller_t *ui, sys_status_t *status, sys_status_ctx_t *ctx);

#endif

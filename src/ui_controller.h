#ifndef UI_CONTROLLER_H
#define UI_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "event_queue.h"

typedef struct {
    bool power_on;
    app_event_type_t last_event;
    uint64_t tick_count;
    bool needs_render;
} ui_controller_t;

void ui_controller_init(ui_controller_t *ui);
void ui_controller_handle_event(ui_controller_t *ui, const app_event_t *event);
bool ui_controller_render(ui_controller_t *ui);

#endif

/*
 * UI controller for ADR0006 (single-thread uloop)
 */
#ifndef UI_CONTROLLER_H
#define UI_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "page_controller.h"
#include "sys_status.h"

#define UI_TICK_ANIM_MS    20
#define UI_TICK_STATIC_MS  1000
#define UI_TICK_IDLE_MS    0
#define UI_AUTO_SLEEP_MS   30000

typedef struct {
    page_controller_t page_ctrl;
    sys_status_t status;
    sys_status_ctx_t *status_ctx;
    bool needs_render;
    bool power_on;
} ui_controller_t;

void ui_controller_init(ui_controller_t *ui);
void ui_controller_cleanup(ui_controller_t *ui);

bool ui_controller_handle_button(ui_controller_t *ui, uint8_t key, bool long_press, uint64_t now_ms);
bool ui_controller_tick(ui_controller_t *ui, uint64_t now_ms);
bool ui_controller_render(ui_controller_t *ui, uint64_t now_ms);
int ui_controller_next_timeout_ms(const ui_controller_t *ui);

#endif

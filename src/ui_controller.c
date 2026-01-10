/*
 * UI controller for ADR0006 (single-thread uloop)
 */
#include "ui_controller.h"

#include <string.h>

#include "hal/display_hal.h"
#include "pages/pages.h"

void ui_controller_init(ui_controller_t *ui) {
    if (!ui) return;

    memset(ui, 0, sizeof(*ui));
    ui->power_on = true;
    ui->needs_render = true;

    /* Initialize page controller with registered pages */
    int page_count = 0;
    const page_t **pages = pages_get_list(&page_count);
    page_controller_init(&ui->page_ctrl, pages, page_count);
    page_controller_set_idle_timeout(&ui->page_ctrl, UI_AUTO_SLEEP_MS);

    /* Initialize system status context */
    ui->status_ctx = sys_status_init();
    if (ui->status_ctx) {
        sys_status_update_local(ui->status_ctx, &ui->status);
    }
}

void ui_controller_cleanup(ui_controller_t *ui) {
    if (!ui) return;

    page_controller_destroy(&ui->page_ctrl);
    sys_status_cleanup(ui->status_ctx);
    ui->status_ctx = NULL;
}

bool ui_controller_handle_button(ui_controller_t *ui, uint8_t key, bool long_press, uint64_t now_ms) {
    if (!ui) return false;

    bool changed = page_controller_handle_key(&ui->page_ctrl, key, long_press, now_ms);
    ui->power_on = page_controller_is_screen_on(&ui->page_ctrl);

    if (changed && ui->power_on) {
        ui->needs_render = true;
    } else if (changed && !ui->power_on) {
        /* Screen turned off */
        ui->needs_render = true;
    }

    return changed;
}

bool ui_controller_tick(ui_controller_t *ui, uint64_t now_ms) {
    if (!ui) return false;

    bool needs_render = page_controller_tick(&ui->page_ctrl, now_ms);
    ui->power_on = page_controller_is_screen_on(&ui->page_ctrl);

    /* Update status only in static mode to keep speeds stable */
    if (ui->power_on && !page_controller_is_animating(&ui->page_ctrl) && ui->status_ctx) {
        sys_status_update_local(ui->status_ctx, &ui->status);
        needs_render = true;
    }

    if (needs_render) {
        ui->needs_render = true;
    }
    return needs_render;
}

bool ui_controller_render(ui_controller_t *ui, uint64_t now_ms) {
    if (!ui || !ui->needs_render) {
        return false;
    }

    if (!display_hal) {
        ui->needs_render = false;
        return false;
    }

    if (!ui->power_on) {
        if (display_hal->set_power) {
            display_hal->set_power(false);
        }
        ui->needs_render = false;
        return true;
    }

    u8g2_t *u8g2 = display_hal->get_u8g2 ? display_hal->get_u8g2() : NULL;
    if (!u8g2) {
        ui->needs_render = false;
        return false;
    }

    if (display_hal->clear_buffer) {
        display_hal->clear_buffer();
    }

    page_controller_render(&ui->page_ctrl, u8g2, &ui->status, now_ms);

    if (display_hal->send_buffer) {
        display_hal->send_buffer();
    }

    if (display_hal->set_power) {
        display_hal->set_power(true);
    }

    ui->needs_render = false;
    return true;
}

int ui_controller_next_timeout_ms(const ui_controller_t *ui) {
    if (!ui) return UI_TICK_STATIC_MS;

    if (!page_controller_is_screen_on(&ui->page_ctrl)) {
        return UI_TICK_IDLE_MS;
    }

    if (page_controller_is_animating(&ui->page_ctrl)) {
        return UI_TICK_ANIM_MS;
    }

    return UI_TICK_STATIC_MS;
}

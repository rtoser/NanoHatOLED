#include "ui_controller.h"

#include <stdio.h>
#include <string.h>

#include "hal/display_hal.h"
#include "pages/pages.h"

#define UI_AUTO_SLEEP_MS_DEFAULT 30000

/* Convert nanoseconds to milliseconds */
static uint64_t ns_to_ms(uint64_t ns) {
    return ns / 1000000ULL;
}

/* Convert event type to key code */
static uint8_t event_to_key(app_event_type_t type) {
    switch (type) {
        case EVT_BTN_K1_SHORT:
        case EVT_BTN_K1_LONG:
            return KEY_K1;
        case EVT_BTN_K2_SHORT:
        case EVT_BTN_K2_LONG:
            return KEY_K2;
        case EVT_BTN_K3_SHORT:
        case EVT_BTN_K3_LONG:
            return KEY_K3;
        default:
            return 0;
    }
}

static bool is_long_press(app_event_type_t type) {
    return type == EVT_BTN_K1_LONG ||
           type == EVT_BTN_K2_LONG ||
           type == EVT_BTN_K3_LONG;
}

static bool is_button_event(app_event_type_t type) {
    return type == EVT_BTN_K1_SHORT || type == EVT_BTN_K2_SHORT || type == EVT_BTN_K3_SHORT ||
           type == EVT_BTN_K1_LONG || type == EVT_BTN_K2_LONG || type == EVT_BTN_K3_LONG;
}

void ui_controller_init(ui_controller_t *ui) {
    if (!ui) {
        return;
    }
    memset(ui, 0, sizeof(*ui));

    ui->power_on = true;
    ui->last_event = EVT_NONE;
    ui->tick_count = 0;
    ui->needs_render = true;
    ui->last_input_ns = 0;
    ui->idle_timeout_ms = UI_AUTO_SLEEP_MS_DEFAULT;

    /* Initialize display HAL */
    if (display_hal && display_hal->init) {
        display_hal->init();
    }

    /* Initialize page controller with pages */
    int page_count;
    const page_t **pages = pages_get_list(&page_count);
    page_controller_init(&ui->page_ctrl, pages, page_count);
    page_controller_set_idle_timeout(&ui->page_ctrl, ui->idle_timeout_ms);
}

void ui_controller_set_status(ui_controller_t *ui, sys_status_t *status, sys_status_ctx_t *ctx) {
    if (!ui) return;
    ui->sys_status = status;
    ui->sys_status_ctx = ctx;
}

void ui_controller_handle_event(ui_controller_t *ui, const app_event_t *event) {
    if (!ui || !event) {
        return;
    }

    ui->last_event = event->type;
    uint64_t now_ms = ns_to_ms(event->timestamp_ns);
    ui->last_event_ms = now_ms;  /* Store for render */

    if (event->type == EVT_TICK) {
        uint32_t step = event->data ? event->data : 1;
        ui->tick_count += step;

        /* Process ubus results on every tick (non-blocking).
         * Status update is controlled by ui_thread based on tick mode:
         * - Idle mode (1000ms): update status
         * - Animation mode (50ms): skip status update to avoid inaccurate speed calculation
         */
        if (ui->sys_status && ui->sys_status_ctx) {
            sys_status_process_results(ui->sys_status_ctx, ui->sys_status);
        }

        /* Let page controller handle tick for auto-sleep and animations */
        if (page_controller_tick(&ui->page_ctrl, now_ms)) {
            ui->needs_render = true;
        }

        /* Sync power state from page controller */
        ui->power_on = page_controller_is_screen_on(&ui->page_ctrl);

    } else if (event->type == EVT_SHUTDOWN) {
        ui->power_on = false;

    } else if (is_button_event(event->type)) {
        uint8_t key = event_to_key(event->type);
        bool long_press = is_long_press(event->type);

        if (page_controller_handle_key(&ui->page_ctrl, key, long_press, now_ms)) {
            ui->needs_render = true;
        }

        /* Sync power state from page controller */
        ui->power_on = page_controller_is_screen_on(&ui->page_ctrl);
    }

    /* Always mark needs_render for visible changes */
    if (ui->power_on) {
        ui->needs_render = true;
    }
}

bool ui_controller_render(ui_controller_t *ui) {
    if (!ui || !ui->needs_render) {
        return false;
    }

    if (!display_hal) {
        ui->needs_render = false;
        return false;
    }

    /* If display is being turned off, do it immediately */
    if (!ui->power_on) {
        if (display_hal->set_power) {
            display_hal->set_power(false);
        }
        ui->needs_render = false;
        return true;
    }

    /* Get u8g2 context from display HAL */
    u8g2_t *u8g2 = display_hal->get_u8g2 ? display_hal->get_u8g2() : NULL;
    if (!u8g2) {
        ui->needs_render = false;
        return false;
    }

    /* Clear buffer */
    if (display_hal->clear_buffer) {
        display_hal->clear_buffer();
    }

    /* Use real timestamp from last event for animations */
    uint64_t now_ms = ui->last_event_ms;

    /* Render via page controller */
    page_controller_render(&ui->page_ctrl, u8g2, ui->sys_status, now_ms);

    /* Send buffer to display BEFORE turning on power */
    if (display_hal->send_buffer) {
        display_hal->send_buffer();
    }

    /* Now turn on display power (after buffer is ready) */
    if (display_hal->set_power) {
        display_hal->set_power(true);
    }

    ui->needs_render = false;
    return true;
}

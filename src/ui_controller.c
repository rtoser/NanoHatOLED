#include "ui_controller.h"

#include <stdio.h>
#include <string.h>

#include "hal/display_hal.h"

static const char *event_name(app_event_type_t type) {
    switch (type) {
        case EVT_BTN_K1_SHORT: return "K1_SHORT";
        case EVT_BTN_K2_SHORT: return "K2_SHORT";
        case EVT_BTN_K3_SHORT: return "K3_SHORT";
        case EVT_BTN_K1_LONG: return "K1_LONG";
        case EVT_BTN_K2_LONG: return "K2_LONG";
        case EVT_BTN_K3_LONG: return "K3_LONG";
        case EVT_TICK: return "TICK";
        case EVT_SHUTDOWN: return "SHUTDOWN";
        default: return "NONE";
    }
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
}

void ui_controller_handle_event(ui_controller_t *ui, const app_event_t *event) {
    if (!ui || !event) {
        return;
    }

    ui->last_event = event->type;

    if (event->type == EVT_TICK) {
        uint32_t step = event->data ? event->data : 1;
        ui->tick_count += step;
    } else if (event->type == EVT_BTN_K2_SHORT) {
        ui->power_on = !ui->power_on;
    } else if (event->type == EVT_SHUTDOWN) {
        ui->power_on = false;
    }

    ui->needs_render = true;
}

bool ui_controller_render(ui_controller_t *ui) {
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

    if (display_hal->set_power) {
        display_hal->set_power(true);
    }

    if (display_hal->begin_frame) {
        display_hal->begin_frame();
    }

    if (display_hal->draw_text) {
        char buf[64];
        snprintf(buf, sizeof(buf), "EVT: %s", event_name(ui->last_event));
        display_hal->draw_text(0, 0, buf);
        snprintf(buf, sizeof(buf), "TICK: %llu", (unsigned long long)ui->tick_count);
        display_hal->draw_text(0, 12, buf);
    }

    if (display_hal->end_frame) {
        display_hal->end_frame();
    }

    ui->needs_render = false;
    return true;
}

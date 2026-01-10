#include "ui_thread.h"

#include <string.h>

#include "sys_status.h"

#define UI_ANIM_TICK_MS 50
#define UI_IDLE_TICK_MS 1000
#define UI_DEFAULT_ANIM_TICKS 10

static bool ui_thread_is_button_event(app_event_type_t type) {
    return type == EVT_BTN_K1_SHORT || type == EVT_BTN_K2_SHORT || type == EVT_BTN_K3_SHORT ||
           type == EVT_BTN_K1_LONG || type == EVT_BTN_K2_LONG || type == EVT_BTN_K3_LONG;
}

static void ui_thread_apply_tick(ui_thread_t *ui, int tick_ms) {
    if (!ui || !ui->loop) {
        return;
    }
    if (tick_ms == ui->tick_ms) {
        return;
    }
    event_loop_request_tick(ui->loop, tick_ms);
    ui->tick_ms = tick_ms;
}

static void ui_thread_stop_tick(ui_thread_t *ui) {
    if (!ui || !ui->loop || !ui->tick_active) {
        return;
    }
    ui_thread_apply_tick(ui, 0);
    ui->tick_active = false;
    ui->anim_ticks_left = 0;
}

static void ui_thread_start_tick(ui_thread_t *ui) {
    if (!ui || !ui->loop) {
        return;
    }
    ui->tick_active = true;
    ui->anim_ticks_left = UI_DEFAULT_ANIM_TICKS;
    ui_thread_apply_tick(ui, UI_ANIM_TICK_MS);
}

static void ui_thread_switch_idle_tick(ui_thread_t *ui) {
    if (!ui || !ui->loop) {
        return;
    }
    ui->tick_active = true;
    ui->anim_ticks_left = 0;
    ui_thread_apply_tick(ui, UI_IDLE_TICK_MS);
}

static void ui_thread_default_handle(const app_event_t *event, void *user) {
    ui_thread_t *ui = (ui_thread_t *)user;
    if (!ui || !event) {
        return;
    }

    ui_controller_handle_event(&ui->controller, event);
    ui_controller_render(&ui->controller);

    if (!ui->loop) {
        return;
    }

    if (event->type == EVT_SHUTDOWN) {
        ui_thread_stop_tick(ui);
        return;
    }

    if (!ui->controller.power_on) {
        ui_thread_stop_tick(ui);
        return;
    }

    /* Check if page controller is animating */
    bool is_animating = page_controller_is_animating(&ui->controller.page_ctrl);

    if (event->type == EVT_TICK && ui->tick_active) {
        if (is_animating) {
            /* Animation in progress - ensure high frame rate tick is active.
             * This handles the case where button event didn't properly switch tick,
             * or animation was triggered by other means.
             */
            ui->anim_ticks_left = UI_DEFAULT_ANIM_TICKS;
            if (ui->tick_ms != UI_ANIM_TICK_MS) {
                ui_thread_apply_tick(ui, UI_ANIM_TICK_MS);
            }
        } else {
            /* No animation - count down and switch to idle tick */
            uint32_t step = event->data ? event->data : 1;
            if (ui->anim_ticks_left > 0) {
                if ((int)step >= ui->anim_ticks_left) {
                    ui->anim_ticks_left = 0;
                    ui_thread_switch_idle_tick(ui);
                } else {
                    ui->anim_ticks_left -= (int)step;
                }
            }

            /* Update system status in idle mode (when no animation and countdown done).
             * Skip during animation to avoid inaccurate speed calculation.
             */
            if (ui->anim_ticks_left == 0 && ui->controller.sys_status && ui->controller.sys_status_ctx) {
                sys_status_update_local(ui->controller.sys_status_ctx, ui->controller.sys_status);
                sys_status_request_services(ui->controller.sys_status_ctx, ui->controller.sys_status);
            }
        }
        return;
    }

    /* Button event - start high speed tick for potential animation */
    if (ui_thread_is_button_event(event->type)) {
        ui_thread_start_tick(ui);
    }
}

static void *ui_thread_main(void *arg) {
    ui_thread_t *ui = (ui_thread_t *)arg;
    while (atomic_load(&ui->running)) {
        app_event_t evt;
        int ret = event_queue_wait(ui->queue, &evt, -1);
        if (ret <= 0) {
            if (!atomic_load(&ui->running)) {
                break;
            }
            continue;
        }
        if (ui->handler) {
            ui->handler(&evt, ui->user);
        }
        if (evt.type == EVT_SHUTDOWN) {
            break;
        }
    }
    return NULL;
}

static int ui_thread_setup(ui_thread_t *ui, event_queue_t *queue, ui_event_handler_fn handler, void *user) {
    if (!ui || !queue) {
        return -1;
    }
    memset(ui, 0, sizeof(*ui));
    ui->queue = queue;
    ui->handler = handler;
    ui->user = user;
    atomic_store(&ui->running, true);
    ui->tick_ms = 0;
    return 0;
}

static int ui_thread_launch(ui_thread_t *ui) {
    if (pthread_create(&ui->thread, NULL, ui_thread_main, ui) != 0) {
        atomic_store(&ui->running, false);
        return -1;
    }
    return 0;
}

int ui_thread_start(ui_thread_t *ui, event_queue_t *queue, ui_event_handler_fn handler, void *user) {
    if (ui_thread_setup(ui, queue, handler, user) != 0) {
        return -1;
    }
    return ui_thread_launch(ui);
}

int ui_thread_start_default(ui_thread_t *ui, event_queue_t *queue, event_loop_t *loop) {
    if (ui_thread_setup(ui, queue, ui_thread_default_handle, ui) != 0) {
        return -1;
    }
    ui->loop = loop;
    ui_controller_init(&ui->controller);
    ui->tick_active = false;
    ui->anim_ticks_left = 0;

    /* Initial render before starting thread */
    ui_controller_render(&ui->controller);

    if (ui->loop) {
        ui_thread_switch_idle_tick(ui);
    }
    return ui_thread_launch(ui);
}

void ui_thread_set_status(ui_thread_t *ui, sys_status_t *status, sys_status_ctx_t *ctx) {
    if (!ui) {
        return;
    }
    ui_controller_set_status(&ui->controller, status, ctx);
}

void ui_thread_stop(ui_thread_t *ui) {
    if (!ui) {
        return;
    }
    atomic_store(&ui->running, false);
    event_queue_close(ui->queue);
    pthread_join(ui->thread, NULL);
}

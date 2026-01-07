#include "ui_thread.h"

#include <string.h>

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

int ui_thread_start(ui_thread_t *ui, event_queue_t *queue, ui_event_handler_fn handler, void *user) {
    if (!ui || !queue) {
        return -1;
    }
    memset(ui, 0, sizeof(*ui));
    ui->queue = queue;
    ui->handler = handler;
    ui->user = user;
    atomic_store(&ui->running, true);
    if (pthread_create(&ui->thread, NULL, ui_thread_main, ui) != 0) {
        atomic_store(&ui->running, false);
        return -1;
    }
    return 0;
}

void ui_thread_stop(ui_thread_t *ui) {
    if (!ui) {
        return;
    }
    atomic_store(&ui->running, false);
    event_queue_close(ui->queue);
    pthread_join(ui->thread, NULL);
}

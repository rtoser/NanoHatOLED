#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#ifndef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE 200809L
#endif

#include "event_loop.h"

#ifdef __linux__

#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include "hal/gpio_hal.h"
#include "hal/time_hal.h"

static app_event_type_t to_app_event_type(gpio_event_type_t type) {
    switch (type) {
        case GPIO_EVT_BTN_K1_SHORT: return EVT_BTN_K1_SHORT;
        case GPIO_EVT_BTN_K2_SHORT: return EVT_BTN_K2_SHORT;
        case GPIO_EVT_BTN_K3_SHORT: return EVT_BTN_K3_SHORT;
        case GPIO_EVT_BTN_K1_LONG: return EVT_BTN_K1_LONG;
        case GPIO_EVT_BTN_K2_LONG: return EVT_BTN_K2_LONG;
        case GPIO_EVT_BTN_K3_LONG: return EVT_BTN_K3_LONG;
        default: return EVT_NONE;
    }
}

static int event_loop_apply_tick(event_loop_t *loop, int tick_ms) {
    struct itimerspec its;
    memset(&its, 0, sizeof(its));
    if (tick_ms > 0) {
        its.it_interval.tv_sec = tick_ms / 1000;
        its.it_interval.tv_nsec = (tick_ms % 1000) * 1000000L;
        its.it_value = its.it_interval;
    }
    if (timerfd_settime(loop->timer_fd, 0, &its, NULL) < 0) {
        return -1;
    }
    loop->tick_ms = tick_ms;
    return 0;
}

int event_loop_init(event_loop_t *loop, event_queue_t *queue) {
    if (!loop || !queue) {
        return -1;
    }

    memset(loop, 0, sizeof(*loop));
    loop->queue = queue;
    loop->gpio_fd = gpio_hal ? gpio_hal->get_fd() : -1;
    loop->event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    loop->timer_fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    loop->pending_tick_ms = -1;
    loop->tick_ms = 0;

    if (loop->event_fd < 0 || loop->timer_fd < 0) {
        event_loop_cleanup(loop);
        return -1;
    }

    return 0;
}

void event_loop_request_tick(event_loop_t *loop, int tick_ms) {
    if (!loop) {
        return;
    }
    atomic_store(&loop->pending_tick_ms, tick_ms);
    uint64_t v = 1;
    if (loop->event_fd >= 0) {
        write(loop->event_fd, &v, sizeof(v));
    }
}

void event_loop_request_shutdown(event_loop_t *loop) {
    if (!loop) {
        return;
    }
    atomic_store(&loop->shutdown_requested, true);
    uint64_t v = 1;
    if (loop->event_fd >= 0) {
        write(loop->event_fd, &v, sizeof(v));
    }
}

static void event_loop_handle_gpio(event_loop_t *loop) {
    gpio_event_t gpio_evt;
    int ret = 0;
    while ((ret = gpio_hal->wait_event(0, &gpio_evt)) > 0) {
        app_event_t evt = {
            .type = to_app_event_type(gpio_evt.type),
            .line = gpio_evt.line,
            .timestamp_ns = gpio_evt.timestamp_ns,
            .data = 0
        };
        if (evt.type != EVT_NONE) {
            event_queue_push(loop->queue, &evt);
        }
    }
}

static void event_loop_handle_timer(event_loop_t *loop) {
    uint64_t expirations = 0;
    if (read(loop->timer_fd, &expirations, sizeof(expirations)) <= 0) {
        return;
    }
    app_event_t evt = {
        .type = EVT_TICK,
        .line = 0,
        .timestamp_ns = time_hal_now_ns(),
        .data = (uint32_t)expirations
    };
    event_queue_push(loop->queue, &evt);
}

static void event_loop_handle_control(event_loop_t *loop) {
    uint64_t v = 0;
    while (read(loop->event_fd, &v, sizeof(v)) > 0) {
        if (v == 0) {
            break;
        }
    }

    int pending_tick = atomic_exchange(&loop->pending_tick_ms, -1);
    if (pending_tick >= 0) {
        event_loop_apply_tick(loop, pending_tick);
    }

    if (atomic_load(&loop->shutdown_requested)) {
        app_event_t evt = {
            .type = EVT_SHUTDOWN,
            .line = 0,
            .timestamp_ns = time_hal_now_ns(),
            .data = 0
        };
        event_queue_push(loop->queue, &evt);
        atomic_store(&loop->running, false);
    }
}

int event_loop_run(event_loop_t *loop) {
    if (!loop || loop->event_fd < 0 || loop->timer_fd < 0) {
        return -1;
    }

    atomic_store(&loop->running, true);

    while (atomic_load(&loop->running)) {
        struct pollfd fds[3];
        nfds_t nfds = 0;

        if (loop->gpio_fd >= 0) {
            fds[nfds].fd = loop->gpio_fd;
            fds[nfds].events = POLLIN;
            nfds++;
        }

        fds[nfds].fd = loop->timer_fd;
        fds[nfds].events = POLLIN;
        nfds++;

        fds[nfds].fd = loop->event_fd;
        fds[nfds].events = POLLIN;
        nfds++;

        int ret = poll(fds, nfds, -1);
        if (ret < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }

        nfds_t idx = 0;
        if (loop->gpio_fd >= 0) {
            if (fds[idx].revents & POLLIN) {
                event_loop_handle_gpio(loop);
            }
            idx++;
        }

        if (fds[idx].revents & POLLIN) {
            event_loop_handle_timer(loop);
        }
        idx++;

        if (fds[idx].revents & POLLIN) {
            event_loop_handle_control(loop);
        }
    }

    return 0;
}

void event_loop_cleanup(event_loop_t *loop) {
    if (!loop) {
        return;
    }
    if (loop->timer_fd >= 0) {
        close(loop->timer_fd);
        loop->timer_fd = -1;
    }
    if (loop->event_fd >= 0) {
        close(loop->event_fd);
        loop->event_fd = -1;
    }
    loop->gpio_fd = -1;
    loop->queue = NULL;
}

#else

int event_loop_init(event_loop_t *loop, event_queue_t *queue) {
    (void)loop;
    (void)queue;
    return -1;
}

int event_loop_run(event_loop_t *loop) {
    (void)loop;
    return -1;
}

void event_loop_request_tick(event_loop_t *loop, int tick_ms) {
    (void)loop;
    (void)tick_ms;
}

void event_loop_request_shutdown(event_loop_t *loop) {
    (void)loop;
}

void event_loop_cleanup(event_loop_t *loop) {
    (void)loop;
}

#endif

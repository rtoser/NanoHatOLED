/*
 * GPIO Button Driver using sysfs interface
 *
 * Uses both-edge detection for reliable button handling:
 * - Short press: reported on release (if held < LONG_PRESS_MS)
 * - Long press: reported while held (after LONG_PRESS_MS)
 *
 * FIX: Event-driven sysfs handling and active-level auto-detect
 */

#include "gpio_button.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <time.h>
#include <errno.h>

#define GPIO_PATH "/sys/class/gpio"
#define NUM_BUTTONS 3
#define LONG_PRESS_MS 600
#define DEBOUNCE_MS 30

static int btn_fds[NUM_BUTTONS] = {-1, -1, -1};
static const int btn_gpios[NUM_BUTTONS] = {BTN_K1_GPIO, BTN_K2_GPIO, BTN_K3_GPIO};
static uint64_t btn_press_time[NUM_BUTTONS] = {0, 0, 0};
static bool btn_pressed[NUM_BUTTONS] = {false, false, false};
static bool btn_long_reported[NUM_BUTTONS] = {false, false, false};
static uint64_t btn_last_edge_ms[NUM_BUTTONS] = {0, 0, 0};
static char pressed_value = '0';

static uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int gpio_export(int gpio) {
    char path[64];
    snprintf(path, sizeof(path), "%s/gpio%d", GPIO_PATH, gpio);

    if (access(path, F_OK) == 0) {
        return 0;
    }

    int fd = open(GPIO_PATH "/export", O_WRONLY);
    if (fd < 0) {
        perror("Failed to open GPIO export");
        return -1;
    }

    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%d", gpio);
    if (write(fd, buf, len) != len) {
        if (errno != EBUSY) {
            perror("Failed to export GPIO");
            close(fd);
            return -1;
        }
    }
    close(fd);
    usleep(100000);
    return 0;
}

static int gpio_set_direction(int gpio, const char *dir) {
    char path[64];
    snprintf(path, sizeof(path), "%s/gpio%d/direction", GPIO_PATH, gpio);

    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open GPIO direction");
        return -1;
    }

    if (write(fd, dir, strlen(dir)) < 0) {
        perror("Failed to set GPIO direction");
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int gpio_set_edge(int gpio, const char *edge) {
    char path[64];
    snprintf(path, sizeof(path), "%s/gpio%d/edge", GPIO_PATH, gpio);

    int fd = open(path, O_WRONLY);
    if (fd < 0) {
        perror("Failed to open GPIO edge");
        return -1;
    }

    if (write(fd, edge, strlen(edge)) < 0) {
        perror("Failed to set GPIO edge");
        close(fd);
        return -1;
    }
    close(fd);
    return 0;
}

static int gpio_open_value(int gpio) {
    char path[64];
    snprintf(path, sizeof(path), "%s/gpio%d/value", GPIO_PATH, gpio);
    return open(path, O_RDONLY);
}

static void gpio_unexport(int gpio) {
    int fd = open(GPIO_PATH "/unexport", O_WRONLY);
    if (fd < 0) return;

    char buf[16];
    int len = snprintf(buf, sizeof(buf), "%d", gpio);
    write(fd, buf, len);
    close(fd);
}

static char gpio_read_value(int idx) {
    if (idx < 0 || idx >= NUM_BUTTONS || btn_fds[idx] < 0) {
        return '1';
    }
    char val = '1';
    lseek(btn_fds[idx], 0, SEEK_SET);
    read(btn_fds[idx], &val, 1);
    return val;
}

static char detect_pressed_value(void) {
    int zeros = 0;
    int ones = 0;

    for (int i = 0; i < NUM_BUTTONS; i++) {
        char val = gpio_read_value(i);
        if (val == '0') {
            zeros++;
        } else {
            ones++;
        }
    }

    // Majority low implies idle-low wiring, so pressed reads high.
    return (zeros >= ones) ? '1' : '0';
}

int gpio_button_init(void) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (gpio_export(btn_gpios[i]) < 0) {
            fprintf(stderr, "Failed to export GPIO %d\n", btn_gpios[i]);
            gpio_button_cleanup();
            return -1;
        }

        if (gpio_set_direction(btn_gpios[i], "in") < 0) {
            fprintf(stderr, "Failed to set GPIO %d direction\n", btn_gpios[i]);
            gpio_button_cleanup();
            return -1;
        }

        // Use both edges: detect press (falling) and release (rising)
        if (gpio_set_edge(btn_gpios[i], "both") < 0) {
            fprintf(stderr, "Failed to set GPIO %d edge\n", btn_gpios[i]);
            gpio_button_cleanup();
            return -1;
        }

        btn_fds[i] = gpio_open_value(btn_gpios[i]);
        if (btn_fds[i] < 0) {
            fprintf(stderr, "Failed to open GPIO %d value\n", btn_gpios[i]);
            gpio_button_cleanup();
            return -1;
        }

    }

    pressed_value = detect_pressed_value();
    uint64_t now = get_time_ms();
    for (int i = 0; i < NUM_BUTTONS; i++) {
        char val = gpio_read_value(i);
        btn_pressed[i] = (val == pressed_value);
        btn_press_time[i] = btn_pressed[i] ? now : 0;
        btn_long_reported[i] = false;
        btn_last_edge_ms[i] = 0;
    }

    return 0;
}

void gpio_button_cleanup(void) {
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (btn_fds[i] >= 0) {
            close(btn_fds[i]);
            btn_fds[i] = -1;
        }
        gpio_unexport(btn_gpios[i]);
        btn_pressed[i] = false;
        btn_long_reported[i] = false;
        btn_press_time[i] = 0;
        btn_last_edge_ms[i] = 0;
    }
}

button_event_t gpio_button_wait(int timeout_ms) {
    struct pollfd pfd[NUM_BUTTONS];
    button_event_t result = BTN_NONE;
    uint64_t now = get_time_ms();

    // Calculate effective timeout for long press detection
    int effective_timeout = timeout_ms;
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (btn_pressed[i] && !btn_long_reported[i]) {
            uint64_t elapsed = now - btn_press_time[i];
            if (elapsed < LONG_PRESS_MS) {
                int remaining = LONG_PRESS_MS - elapsed;
                if (effective_timeout < 0 || remaining < effective_timeout) {
                    effective_timeout = remaining;
                }
            }
        }
    }

    // Setup poll
    for (int i = 0; i < NUM_BUTTONS; i++) {
        pfd[i].fd = btn_fds[i];
        pfd[i].events = POLLPRI | POLLERR;
    }

    int ret = poll(pfd, NUM_BUTTONS, effective_timeout);
    if (ret < 0) {
        if (errno != EINTR) {
            return BTN_NONE;
        }
        ret = 0;
    }
    now = get_time_ms();

    // Process buttons with events only to avoid clearing pending edges
    for (int i = 0; i < NUM_BUTTONS; i++) {
        bool had_event = (ret > 0) && (pfd[i].revents & (POLLPRI | POLLERR));
        if (!had_event) {
            continue;
        }

        char val = gpio_read_value(i);
        bool currently_pressed = (val == pressed_value);

        if (btn_last_edge_ms[i] != 0 && now - btn_last_edge_ms[i] < DEBOUNCE_MS) {
            continue;
        }
        btn_last_edge_ms[i] = now;

        if (currently_pressed) {
            if (!btn_pressed[i]) {
                btn_pressed[i] = true;
                btn_press_time[i] = now;
                btn_long_reported[i] = false;
            }
        } else {
            if (btn_pressed[i]) {
                btn_pressed[i] = false;
                if (!btn_long_reported[i] && result == BTN_NONE) {
                    result = (button_event_t)(BTN_K1_PRESS + i);
                }
            } else {
                if (result == BTN_NONE) {
                    result = (button_event_t)(BTN_K1_PRESS + i);
                }
            }
        }
    }

    // Check for long press (button still held past threshold)
    for (int i = 0; i < NUM_BUTTONS; i++) {
        if (btn_pressed[i] && !btn_long_reported[i]) {
            if (now - btn_press_time[i] >= LONG_PRESS_MS) {
                btn_long_reported[i] = true;
                result = (button_event_t)(BTN_K1_LONG_PRESS + i);
            }
        }
    }

    return result;
}

button_event_t gpio_button_poll(void) {
    return gpio_button_wait(0);
}

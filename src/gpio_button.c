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
#define LONG_PRESS_MS 600

static int btn_fds[3] = {-1, -1, -1};
static int btn_gpios[3] = {BTN_K1_GPIO, BTN_K2_GPIO, BTN_K3_GPIO};
static uint64_t btn_press_time[3] = {0, 0, 0};
static bool btn_pressed[3] = {false, false, false};
static bool btn_long_reported[3] = {false, false, false};

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

int gpio_button_init(void) {
    for (int i = 0; i < 3; i++) {
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

        // Set falling edge only (button press, active low)
        if (gpio_set_edge(btn_gpios[i], "falling") < 0) {
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

        // Clear any pending interrupt
        char val;
        lseek(btn_fds[i], 0, SEEK_SET);
        read(btn_fds[i], &val, 1);
    }

    return 0;
}

void gpio_button_cleanup(void) {
    for (int i = 0; i < 3; i++) {
        if (btn_fds[i] >= 0) {
            close(btn_fds[i]);
            btn_fds[i] = -1;
        }
        gpio_unexport(btn_gpios[i]);
    }
}

bool gpio_button_read(int gpio) {
    int idx = -1;
    for (int i = 0; i < 3; i++) {
        if (btn_gpios[i] == gpio) {
            idx = i;
            break;
        }
    }

    if (idx < 0 || btn_fds[idx] < 0) return false;

    char val;
    lseek(btn_fds[idx], 0, SEEK_SET);
    if (read(btn_fds[idx], &val, 1) != 1) return false;

    // Buttons are active low
    return val == '0';
}

// Wait for button event with interrupt (poll), timeout in ms
// Using falling edge only - each interrupt = one button press
button_event_t gpio_button_wait(int timeout_ms) {
    struct pollfd pfd[3];

    for (int i = 0; i < 3; i++) {
        pfd[i].fd = btn_fds[i];
        pfd[i].events = POLLPRI | POLLERR;
    }

    int ret = poll(pfd, 3, timeout_ms);
    uint64_t now = get_time_ms();

    // Check which button triggered
    for (int i = 0; i < 3; i++) {
        // Always read to clear interrupt
        char val;
        lseek(btn_fds[i], 0, SEEK_SET);
        read(btn_fds[i], &val, 1);

        if (ret > 0 && (pfd[i].revents & POLLPRI)) {
            // Falling edge interrupt = button pressed
            // Check for long press tracking
            if (!btn_pressed[i]) {
                btn_pressed[i] = true;
                btn_press_time[i] = now;
                btn_long_reported[i] = false;
                return (button_event_t)(BTN_K1_PRESS + i);
            }
        }

        // Update pressed state based on current value
        bool currently_pressed = (val == '0');
        if (!currently_pressed) {
            btn_pressed[i] = false;
        }

        // Check for long press (button still held)
        if (btn_pressed[i] && !btn_long_reported[i]) {
            if (now - btn_press_time[i] >= LONG_PRESS_MS) {
                btn_long_reported[i] = true;
                return (button_event_t)(BTN_K1_LONG_PRESS + i);
            }
        }
    }

    return BTN_NONE;
}

button_event_t gpio_button_poll(void) {
    return gpio_button_wait(0);  // Non-blocking
}

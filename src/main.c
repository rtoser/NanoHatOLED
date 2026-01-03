#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <time.h>
#include "u8g2_port_linux.h"
#include "gpio_button.h"
#include "sys_status.h"

#define APP_NAME "nanohat-oled"
#define VERSION "1.0.0"

// Display pages
typedef enum {
    PAGE_STATUS = 0,
    PAGE_NETWORK,
    PAGE_SERVICES,
    PAGE_SYSTEM,
    PAGE_COUNT
} page_t;

static volatile int running = 1;
static u8g2_t u8g2;
static page_t current_page = PAGE_NETWORK;
static sys_status_t sys_status;
static int display_on = 1;

static void signal_handler(int sig) {
    syslog(LOG_INFO, "Received signal %d, shutting down...", sig);
    running = 0;
}

static void setup_signals(void) {
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
}

static void draw_header(const char *title) {
    u8g2_SetFont(&u8g2, u8g2_font_8x13B_tf);  // Bold title
    u8g2_DrawStr(&u8g2, 0, 11, title);

    // Page indicator in top right
    char indicator[8];
    snprintf(indicator, sizeof(indicator), "%d/%d", current_page + 1, PAGE_COUNT);
    u8g2_SetFont(&u8g2, u8g2_font_5x7_tf);
    int w = u8g2_GetStrWidth(&u8g2, indicator);
    u8g2_DrawStr(&u8g2, 128 - w, 11, indicator);

    u8g2_DrawHLine(&u8g2, 0, 13, 128);
}

static void draw_page_status(void) {
    char buf[32];

    draw_header(sys_status.hostname);

    u8g2_SetFont(&u8g2, u8g2_font_7x13_tf);

    // CPU
    snprintf(buf, sizeof(buf), "CPU: %.1f%% %.0fC", sys_status.cpu_usage, sys_status.cpu_temp);
    u8g2_DrawStr(&u8g2, 0, 28, buf);

    // Memory
    float mem_used_pct = 100.0f * (1.0f - (float)sys_status.mem_available / sys_status.mem_total);
    snprintf(buf, sizeof(buf), "MEM: %.1f%%", mem_used_pct);
    u8g2_DrawStr(&u8g2, 0, 43, buf);

    // Uptime
    char uptime_str[16];
    sys_status_format_uptime(sys_status.uptime, uptime_str, sizeof(uptime_str));
    snprintf(buf, sizeof(buf), "UPT: %s", uptime_str);
    u8g2_DrawStr(&u8g2, 0, 58, buf);
}

static void draw_page_network(void) {
    char buf[32];

    draw_header("Network");

    u8g2_SetFont(&u8g2, u8g2_font_7x13_tf);

    // IP
    snprintf(buf, sizeof(buf), "IP: %s", sys_status.ip_addr);
    u8g2_DrawStr(&u8g2, 0, 28, buf);

    // RX speed
    char rx_speed[12];
    sys_status_format_bytes(sys_status.rx_speed, rx_speed, sizeof(rx_speed));
    snprintf(buf, sizeof(buf), "RX: %s/s", rx_speed);
    u8g2_DrawStr(&u8g2, 0, 43, buf);

    // TX speed
    char tx_speed[12];
    sys_status_format_bytes(sys_status.tx_speed, tx_speed, sizeof(tx_speed));
    snprintf(buf, sizeof(buf), "TX: %s/s", tx_speed);
    u8g2_DrawStr(&u8g2, 0, 58, buf);
}

static void draw_page_services(void) {
    draw_header("Services");

    u8g2_SetFont(&u8g2, u8g2_font_7x13_tf);

    int y = 28;
    for (int i = 0; i < sys_status.service_count && i < 3; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s: %s",
                 sys_status.services[i].name,
                 sys_status.services[i].running ? "ON" : "OFF");
        u8g2_DrawStr(&u8g2, 0, y, buf);
        y += 15;
    }
}

static void draw_page_system(void) {
    char buf[32];

    draw_header("System");

    u8g2_SetFont(&u8g2, u8g2_font_7x13_tf);

    // Memory details
    snprintf(buf, sizeof(buf), "Total: %lu MB", sys_status.mem_total / 1024);
    u8g2_DrawStr(&u8g2, 0, 28, buf);

    snprintf(buf, sizeof(buf), "Avail: %lu MB", sys_status.mem_available / 1024);
    u8g2_DrawStr(&u8g2, 0, 43, buf);

    snprintf(buf, sizeof(buf), "Free:  %lu MB", sys_status.mem_free / 1024);
    u8g2_DrawStr(&u8g2, 0, 58, buf);
}

static void draw_current_page(void) {
    if (!display_on) return;

    u8g2_ClearBuffer(&u8g2);

    switch (current_page) {
        case PAGE_STATUS:
            draw_page_status();
            break;
        case PAGE_NETWORK:
            draw_page_network();
            break;
        case PAGE_SERVICES:
            draw_page_services();
            break;
        case PAGE_SYSTEM:
            draw_page_system();
            break;
        default:
            break;
    }

    u8g2_SendBuffer(&u8g2);
}

static void toggle_display(void) {
    display_on = !display_on;
    u8g2_SetPowerSave(&u8g2, !display_on);
    syslog(LOG_INFO, "Display %s", display_on ? "ON" : "OFF");
    if (display_on) {
        draw_current_page();
    }
}

static void handle_button(button_event_t event) {
    switch (event) {
        case BTN_K1_PRESS:  // Previous page
            if (!display_on) {
                toggle_display();
                return;
            }
            if (current_page > 0) {
                current_page--;
            } else {
                current_page = PAGE_COUNT - 1;
            }
            syslog(LOG_DEBUG, "Button K1: page %d", current_page);
            break;

        case BTN_K3_PRESS:  // Next page
            if (!display_on) {
                toggle_display();
                return;
            }
            current_page = (current_page + 1) % PAGE_COUNT;
            syslog(LOG_DEBUG, "Button K3: page %d", current_page);
            break;

        case BTN_K2_PRESS:  // Toggle display
            toggle_display();
            return;

        default:
            return;
    }

    draw_current_page();
}

static int display_init(void) {
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &u8g2,
        U8G2_R0,
        u8x8_byte_linux_i2c,
        u8g2_gpio_and_delay_linux
    );

    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);

    return 0;
}

static void display_shutdown(void) {
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(&u8g2, 20, 35, "Shutting down...");
    u8g2_SendBuffer(&u8g2);
    usleep(500000);
    u8g2_SetPowerSave(&u8g2, 1);
}

int main(int argc, char *argv[]) {
    int daemonize = 0;

    // Parse arguments
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-d") == 0 || strcmp(argv[i], "--daemon") == 0) {
            daemonize = 1;
        } else if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
            printf("%s version %s\n", APP_NAME, VERSION);
            return 0;
        } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            printf("Usage: %s [options]\n", APP_NAME);
            printf("Options:\n");
            printf("  -d, --daemon    Run as daemon\n");
            printf("  -v, --version   Show version\n");
            printf("  -h, --help      Show this help\n");
            return 0;
        }
    }

    // Setup syslog
    openlog(APP_NAME, LOG_PID | LOG_CONS, LOG_DAEMON);
    syslog(LOG_INFO, "Starting %s v%s", APP_NAME, VERSION);

    // Daemonize if requested
    if (daemonize) {
        pid_t pid = fork();
        if (pid < 0) {
            syslog(LOG_ERR, "Fork failed");
            return 1;
        }
        if (pid > 0) {
            return 0;  // Parent exits
        }
        setsid();
    }

    // Setup signal handlers
    setup_signals();

    // Initialize GPIO buttons
    if (gpio_button_init() < 0) {
        syslog(LOG_WARNING, "GPIO buttons init failed, continuing without buttons");
    } else {
        syslog(LOG_INFO, "GPIO buttons initialized");
    }

    // Initialize display
    if (display_init() < 0) {
        syslog(LOG_ERR, "Display init failed");
        gpio_button_cleanup();
        closelog();
        return 1;
    }
    syslog(LOG_INFO, "Display initialized");

    // Main loop - use interrupt-driven button wait
    time_t last_update = 0;
    while (running) {
        time_t now = time(NULL);

        // Update system status every second
        if (now != last_update) {
            last_update = now;
            sys_status_update(&sys_status);
            draw_current_page();
        }

        // Wait for button event (interrupt) or timeout after 100ms
        button_event_t event = gpio_button_wait(100);
        if (event != BTN_NONE) {
            handle_button(event);
        }
    }

    // Cleanup
    syslog(LOG_INFO, "Shutting down...");
    display_shutdown();
    gpio_button_cleanup();
    closelog();

    return 0;
}

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <syslog.h>
#include <time.h>
#include "u8g2_port_linux.h"
#include "gpio_button.h"
#include "sys_status.h"
#include "ubus_service.h"

#define APP_NAME "nanohat-oled"
#define VERSION "1.0.0"
#define MENU_TIMEOUT_MS 10000
#define SERVICE_REFRESH_INTERVAL 5

// Title animation
typedef enum {
    TITLE_ANIM_NONE = 0,
    TITLE_ANIM_ENTER,   // Left -> Center (entering menu)
    TITLE_ANIM_REJECT   // Right push -> bounce back (rejection)
} title_anim_type_t;

#define TITLE_ANIM_ENTER_FRAMES 5
#define TITLE_ANIM_REJECT_FRAMES 11

// Damped spring offsets for reject animation
// Each position held for multiple frames to make bouncing visible at 60Hz
static const int8_t reject_offsets[TITLE_ANIM_REJECT_FRAMES] = {
    35, 35, 35,   // Right +35px, hold ~48ms (hit wall)
    -18, -18,     // Bounce left, hold ~32ms
    12, 12,       // Right again
    -6,           // Left
    3,            // Right (settling)
    0             // Rest
};

// Frame timing
#define FRAME_INTERVAL_MS 100      // 10 Hz normal refresh
#define FRAME_INTERVAL_ANIM_MS 16  // ~60 Hz during animation
#define STATUS_UPDATE_MS 1000      // 1 second status update

// Display pages
typedef enum {
    PAGE_STATUS = 0,
    PAGE_NETWORK,
    PAGE_SERVICES,
    PAGE_SYSTEM,
    PAGE_COUNT
} page_t;

// Page titles for animation calculation
static const char *page_titles[] = {
    "Status",    // PAGE_STATUS - hostname is dynamic, use placeholder
    "Network",   // PAGE_NETWORK
    "Services",  // PAGE_SERVICES
    "System"     // PAGE_SYSTEM
};

static volatile int running = 1;
static u8g2_t u8g2;
static page_t current_page = PAGE_NETWORK;
static sys_status_t sys_status;
static int display_on = 1;
static int menu_active = 0;
static int menu_selected = 0;
static uint64_t menu_last_input_ms = 0;
static time_t last_service_refresh = 0;

// Title animation state
static struct {
    title_anim_type_t type;
    int frame;
    int target_x;  // For enter animation: target center x position
} title_anim = {TITLE_ANIM_NONE, 0, 0};

// Render state
static int dirty = 1;  // Need redraw flag

static inline void mark_dirty(void) {
    dirty = 1;
}

static inline int is_animating(void) {
    return title_anim.type != TITLE_ANIM_NONE;
}

static uint64_t get_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

// Animation settings (optimized for 800KHz I2C)
#define TRANSITION_FRAMES 8
#define TRANSITION_DELAY_US 0  // No artificial delay, pure I2C speed
static int g_x_offset = 0;  // Global x offset for animation

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

// Draw string with global x offset (for animation)
static inline void draw_str(int x, int y, const char *str) {
    u8g2_DrawStr(&u8g2, x + g_x_offset, y, str);
}

static inline void draw_hline(int x, int y, int w) {
    u8g2_DrawHLine(&u8g2, x + g_x_offset, y, w);
}

static inline void draw_box(int x, int y, int w, int h) {
    u8g2_DrawBox(&u8g2, x + g_x_offset, y, w, h);
}

static int page_supports_menu(page_t page);  // Forward declaration
static int get_title_anim_offset(void);       // Forward declaration

static void draw_header(const char *title, int centered, int show_indicator) {
    u8g2_SetFont(&u8g2, u8g2_font_8x13B_tf);  // Bold title

    // Calculate base x position
    int base_x = 0;
    if (centered) {
        int w = u8g2_GetStrWidth(&u8g2, title);
        base_x = (128 - w) / 2;
        if (base_x < 0) base_x = 0;
    }

    // Apply title animation offset
    int anim_offset = get_title_anim_offset();
    int x = base_x + anim_offset;
    if (x < 0) x = 0;

    u8g2_DrawStr(&u8g2, x, 11, title);

    if (show_indicator) {
        // Page indicator in top right
        char indicator[8];
        snprintf(indicator, sizeof(indicator), "%d/%d", current_page + 1, PAGE_COUNT);
        u8g2_SetFont(&u8g2, u8g2_font_5x7_tf);
        int w = u8g2_GetStrWidth(&u8g2, indicator);
        u8g2_DrawStr(&u8g2, 128 - w, 11, indicator);
    }

    u8g2_DrawHLine(&u8g2, 0, 13, 128);
}

// Draw navigation indicator arrows at bottom right
// ▼ = can enter (long press K2), ▲ = can exit (long press K2)
static void draw_nav_indicator(void) {
    int can_enter = page_supports_menu(current_page) && !menu_active;
    int can_exit = menu_active;

    // Arrow dimensions: 7 wide x 4 tall
    int cx = 124;  // center x

    if (can_enter && can_exit) {
        // Both arrows stacked vertically
        // Up arrow: top
        u8g2_DrawTriangle(&u8g2, cx, 52, cx - 3, 56, cx + 3, 56);
        // Down arrow: bottom
        u8g2_DrawTriangle(&u8g2, cx - 3, 58, cx + 3, 58, cx, 62);
    } else if (can_exit) {
        // Up arrow only (exit)
        u8g2_DrawTriangle(&u8g2, cx, 57, cx - 3, 62, cx + 3, 62);
    } else if (can_enter) {
        // Down arrow only (enter)
        u8g2_DrawTriangle(&u8g2, cx - 3, 57, cx + 3, 57, cx, 62);
    }
}

static void draw_page_status(void) {
    char buf[32];

    draw_header(sys_status.hostname, 0, 1);

    u8g2_SetFont(&u8g2, u8g2_font_7x13_te);  // _te includes Latin-1 extended chars

    // CPU (degree symbol U+00B0 = 0xB0 in Latin-1)
    snprintf(buf, sizeof(buf), "CPU: %.1f%% %.0f\xb0""C", sys_status.cpu_usage, sys_status.cpu_temp);
    draw_str(0, 28, buf);

    // Memory (guard against division by zero)
    float mem_used_pct = 0.0f;
    if (sys_status.mem_total > 0) {
        mem_used_pct = 100.0f * (1.0f - (float)sys_status.mem_available / sys_status.mem_total);
    }
    snprintf(buf, sizeof(buf), "MEM: %.1f%%", mem_used_pct);
    draw_str(0, 43, buf);

    // Uptime
    char uptime_str[16];
    sys_status_format_uptime(sys_status.uptime, uptime_str, sizeof(uptime_str));
    snprintf(buf, sizeof(buf), "UPT: %s", uptime_str);
    draw_str(0, 58, buf);
}

static void draw_page_network(void) {
    char buf[32];

    draw_header("Network", 0, 1);

    u8g2_SetFont(&u8g2, u8g2_font_7x13_tf);

    // IP
    snprintf(buf, sizeof(buf), "IP: %s", sys_status.ip_addr);
    draw_str(0, 28, buf);

    // RX speed
    char rx_speed[12];
    sys_status_format_bytes(sys_status.rx_speed, rx_speed, sizeof(rx_speed));
    snprintf(buf, sizeof(buf), "RX: %s/s", rx_speed);
    draw_str(0, 43, buf);

    // TX speed
    char tx_speed[12];
    sys_status_format_bytes(sys_status.tx_speed, tx_speed, sizeof(tx_speed));
    snprintf(buf, sizeof(buf), "TX: %s/s", tx_speed);
    draw_str(0, 58, buf);
}

static void draw_page_services(void) {
    draw_header("Services", 0, 1);

    u8g2_SetFont(&u8g2, u8g2_font_7x13_tf);

    if (sys_status.service_count <= 0) {
        draw_str(0, 40, "No services");
        return;
    }

    int y = 28;
    for (int i = 0; i < sys_status.service_count && i < 3; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s: %s",
                 sys_status.services[i].name,
                 sys_status.services[i].running ? "ON" : "OFF");
        draw_str(0, y, buf);
        y += 15;
    }
}

static void draw_page_services_menu(void) {
    draw_header("Services", 1, 0);

    u8g2_SetFont(&u8g2, u8g2_font_7x13_tf);

    if (sys_status.service_count <= 0) {
        draw_str(0, 40, "No services");
        return;
    }
    if (menu_selected >= sys_status.service_count) {
        menu_selected = 0;
    }

    int y = 28;
    for (int i = 0; i < sys_status.service_count && i < 3; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%s: %s",
                 sys_status.services[i].name,
                 sys_status.services[i].running ? "ON" : "OFF");
        if (i == menu_selected) {
            draw_box(0, y - 11, 128, 13);
            u8g2_SetDrawColor(&u8g2, 0);
            draw_str(0, y, buf);
            u8g2_SetDrawColor(&u8g2, 1);
        } else {
            draw_str(0, y, buf);
        }
        y += 15;
    }
}

static void draw_page_system(void) {
    char buf[32];

    draw_header("System", 0, 1);

    u8g2_SetFont(&u8g2, u8g2_font_7x13_tf);

    // Memory details
    snprintf(buf, sizeof(buf), "Total: %lu MB", sys_status.mem_total / 1024);
    draw_str(0, 28, buf);

    snprintf(buf, sizeof(buf), "Avail: %lu MB", sys_status.mem_available / 1024);
    draw_str(0, 43, buf);

    snprintf(buf, sizeof(buf), "Free:  %lu MB", sys_status.mem_free / 1024);
    draw_str(0, 58, buf);
}

// Render a specific page (used for both normal display and animation)
static void render_page(page_t page) {
    switch (page) {
        case PAGE_STATUS:
            draw_page_status();
            break;
        case PAGE_NETWORK:
            draw_page_network();
            break;
        case PAGE_SERVICES:
            if (menu_active) {
                draw_page_services_menu();
            } else {
                draw_page_services();
            }
            break;
        case PAGE_SYSTEM:
            draw_page_system();
            break;
        default:
            break;
    }

    // Draw navigation indicator (enter/exit arrows)
    draw_nav_indicator();
}

// Get current title x offset for animation
static int get_title_anim_offset(void) {
    if (title_anim.type == TITLE_ANIM_NONE) return 0;

    if (title_anim.type == TITLE_ANIM_REJECT) {
        // Damped spring: lookup table
        if (title_anim.frame < TITLE_ANIM_REJECT_FRAMES) {
            return reject_offsets[title_anim.frame];
        }
        return 0;
    }

    if (title_anim.type == TITLE_ANIM_ENTER) {
        // Linear motion for snappy feel
        float t = (float)title_anim.frame / TITLE_ANIM_ENTER_FRAMES;
        return (int)(t * title_anim.target_x);
    }

    return 0;
}

// Advance animation frame, return true if still animating
static int advance_title_anim(void) {
    if (title_anim.type == TITLE_ANIM_NONE) return 0;

    title_anim.frame++;

    int max_frames = (title_anim.type == TITLE_ANIM_ENTER)
                     ? TITLE_ANIM_ENTER_FRAMES
                     : TITLE_ANIM_REJECT_FRAMES;

    if (title_anim.frame >= max_frames) {
        title_anim.type = TITLE_ANIM_NONE;
        title_anim.frame = 0;
        return 0;
    }
    return 1;
}

// Start title animation
static void start_title_anim(title_anim_type_t type, int target_x) {
    title_anim.type = type;
    title_anim.frame = 0;
    title_anim.target_x = target_x;
    mark_dirty();
}

static void draw_current_page(void) {
    if (!display_on) return;

    u8g2_ClearBuffer(&u8g2);
    render_page(current_page);
    u8g2_SendBuffer(&u8g2);
}

// Pre-render a page to a buffer (1024 bytes for 128x64 monochrome)
static void render_page_to_buffer(page_t page, uint8_t *buf) {
    // Save original buffer pointer
    uint8_t *original_buf = u8g2_GetBufferPtr(&u8g2);

    // Temporarily point u8g2 to our buffer
    // Note: u8g2 buffer structure allows this trick
    u8g2.tile_buf_ptr = buf;

    u8g2_ClearBuffer(&u8g2);
    g_x_offset = 0;
    render_page(page);

    // Restore original buffer
    u8g2.tile_buf_ptr = original_buf;
}

// Slide transition animation using dual-buffer composition
// direction: -1 = slide right (K1, prev page), +1 = slide left (K3, next page)
static void transition_to_page(page_t from, page_t to, int direction) {
    if (!display_on) return;

    // Pre-render both pages to separate buffers
    static uint8_t buf_from[1024];
    static uint8_t buf_to[1024];

    render_page_to_buffer(from, buf_from);
    render_page_to_buffer(to, buf_to);

    uint8_t *display_buf = u8g2_GetBufferPtr(&u8g2);

    // SSD1306 buffer layout: 8 pages × 128 columns = 1024 bytes
    // Each page is 128 bytes (one byte per column, 8 vertical pixels)
    // Step by 8 pixels (8 bytes per page) for efficient memcpy

    int step = 128 / TRANSITION_FRAMES;  // 16 pixels per frame

    for (int frame = 1; frame <= TRANSITION_FRAMES; frame++) {
        int offset = frame * step;  // 16, 32, 48, ... 128

        // Composite the two buffers
        // For each of the 8 pages (each 128 bytes)
        for (int page = 0; page < 8; page++) {
            int page_offset = page * 128;

            if (direction > 0) {
                // Slide left: from page moves left, to page enters from right
                // Copy (128 - offset) bytes from buf_from starting at offset
                // Copy offset bytes from buf_to starting at 0
                memcpy(display_buf + page_offset,
                       buf_from + page_offset + offset,
                       128 - offset);
                memcpy(display_buf + page_offset + (128 - offset),
                       buf_to + page_offset,
                       offset);
            } else {
                // Slide right: from page moves right, to page enters from left
                // Copy offset bytes from buf_to starting at (128 - offset)
                // Copy (128 - offset) bytes from buf_from starting at 0
                memcpy(display_buf + page_offset,
                       buf_to + page_offset + (128 - offset),
                       offset);
                memcpy(display_buf + page_offset + offset,
                       buf_from + page_offset,
                       128 - offset);
            }
        }

        u8g2_SendBuffer(&u8g2);
        usleep(TRANSITION_DELAY_US);
    }

    g_x_offset = 0;
}

static int page_supports_menu(page_t page) {
    return page == PAGE_SERVICES;
}

static void menu_touch(void) {
    menu_last_input_ms = get_time_ms();
}

static void menu_enter(void) {
    menu_active = 1;
    menu_selected = 0;
    menu_touch();
    sys_status_update_services(&sys_status);
    last_service_refresh = time(NULL);
}

static void menu_exit(void) {
    menu_active = 0;
}

static void toggle_display(void) {
    display_on = !display_on;
    u8g2_SetPowerSave(&u8g2, !display_on);
    syslog(LOG_INFO, "Display %s", display_on ? "ON" : "OFF");
    if (display_on) {
        mark_dirty();
    } else {
        // Clear animation state to avoid idle spinning
        title_anim.type = TITLE_ANIM_NONE;
    }
}

static void handle_button(button_event_t event) {
    if (!display_on) {
        toggle_display();
        return;
    }

    if (menu_active) {
        int count = sys_status.service_count;
        if (count > 0 && menu_selected >= count) {
            menu_selected = 0;
        }
        switch (event) {
            case BTN_K1_PRESS:
                if (count > 0) {
                    menu_selected = (menu_selected + count - 1) % count;
                    menu_touch();
                    mark_dirty();
                }
                return;
            case BTN_K3_PRESS:
                if (count > 0) {
                    menu_selected = (menu_selected + 1) % count;
                    menu_touch();
                    mark_dirty();
                }
                return;
            case BTN_K2_PRESS:
                if (count > 0) {
                    service_status_t *svc = &sys_status.services[menu_selected];
                    const char *action = svc->running ? "stop" : "start";
                    syslog(LOG_INFO, "Service %s %s", svc->ubus_name, action);
                    if (ubus_service_action(svc->ubus_name, action) == 0) {
                        // Optimistic update: flip state immediately for responsive UI
                        svc->running = !svc->running;
                    } else {
                        syslog(LOG_ERR, "Service %s %s failed", svc->ubus_name, action);
                    }
                    menu_touch();
                    last_service_refresh = time(NULL);
                    mark_dirty();
                }
                return;
            case BTN_K2_LONG_PRESS:
                menu_exit();
                mark_dirty();
                return;
            default:
                return;
        }
    }

    page_t old_page = current_page;
    page_t new_page;

    switch (event) {
        case BTN_K1_PRESS:  // Previous page (slide right)
            new_page = (current_page > 0) ? current_page - 1 : PAGE_COUNT - 1;
            syslog(LOG_DEBUG, "Button K1: page %d -> %d", current_page, new_page);
            current_page = new_page;
            if (current_page == PAGE_SERVICES || old_page == PAGE_SERVICES) {
                sys_status_update_services(&sys_status);
                last_service_refresh = time(NULL);
            }
            // NOTE: transition is blocking (~160ms at 800kHz I2C) - acceptable for
            // smooth animation UX. Button events during transition are queued by kernel.
            transition_to_page(old_page, new_page, -1);
            return;

        case BTN_K3_PRESS:  // Next page (slide left)
            new_page = (current_page + 1) % PAGE_COUNT;
            syslog(LOG_DEBUG, "Button K3: page %d -> %d", current_page, new_page);
            current_page = new_page;
            if (current_page == PAGE_SERVICES || old_page == PAGE_SERVICES) {
                sys_status_update_services(&sys_status);
                last_service_refresh = time(NULL);
            }
            // NOTE: transition is blocking - see BTN_K1_PRESS comment
            transition_to_page(old_page, new_page, +1);
            return;

        case BTN_K2_PRESS:  // Toggle display
            toggle_display();
            return;

        case BTN_K2_LONG_PRESS:
            syslog(LOG_DEBUG, "K2 long press on page %d", current_page);
            if (page_supports_menu(current_page)) {
                // Do ubus call FIRST (blocking) before animation starts
                menu_enter();
                // Then start animation - no blocking delay affects animation playback
                const char *title = page_titles[current_page];
                u8g2_SetFont(&u8g2, u8g2_font_8x13B_tf);
                int title_w = u8g2_GetStrWidth(&u8g2, title);
                int center_x = (128 - title_w) / 2;
                if (center_x < 0) center_x = 0;
                start_title_anim(TITLE_ANIM_ENTER, center_x);  // Slide to center
                mark_dirty();
            } else {
                syslog(LOG_DEBUG, "Starting reject animation");
                start_title_anim(TITLE_ANIM_REJECT, 0);  // Bounce rejection
                mark_dirty();
            }
            return;

        default:
            return;
    }
}

static int display_init(void) {
    // Check if I2C device is accessible
    if (access("/dev/i2c-0", R_OK | W_OK) != 0) {
        syslog(LOG_ERR, "Cannot access /dev/i2c-0: %m");
        return -1;
    }

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

    // Initialize system status module
    sys_status_init();
    if (ubus_service_init() < 0) {
        syslog(LOG_WARNING, "ubus init failed, service status may be unavailable");
    }

    // Initialize display
    if (display_init() < 0) {
        syslog(LOG_ERR, "Display init failed");
        sys_status_cleanup();
        gpio_button_cleanup();
        closelog();
        return 1;
    }
    syslog(LOG_INFO, "Display initialized");

    sys_status_update_basic(&sys_status);
    sys_status_update_services(&sys_status);
    last_service_refresh = time(NULL);
    draw_current_page();
    dirty = 0;

    // Main loop - time-driven rendering with event-driven input
    uint64_t next_frame_ms = get_time_ms() + FRAME_INTERVAL_MS;
    uint64_t next_status_ms = get_time_ms() + STATUS_UPDATE_MS;
    uint64_t next_service_ms = get_time_ms() + SERVICE_REFRESH_INTERVAL * 1000;

    while (running) {
        uint64_t now_ms = get_time_ms();

        // Calculate timeout: minimum of all pending deadlines
        int64_t timeout = next_frame_ms - now_ms;

        // Also consider status update deadline
        int64_t status_wait = next_status_ms - now_ms;
        if (status_wait < timeout) timeout = status_wait;

        // Service refresh (only on services page)
        if (current_page == PAGE_SERVICES) {
            int64_t service_wait = next_service_ms - now_ms;
            if (service_wait < timeout) timeout = service_wait;
        }

        // Menu timeout check
        if (menu_active) {
            int64_t menu_wait = (menu_last_input_ms + MENU_TIMEOUT_MS) - now_ms;
            if (menu_wait < timeout) timeout = menu_wait;
        }

        // Clamp timeout
        if (timeout < 0) timeout = 0;
        if (timeout > 1000) timeout = 1000;

        // Wait for button event or timeout
        button_event_t event = gpio_button_wait((int)timeout);

        // Handle button immediately (fast path, just updates state)
        if (event != BTN_NONE) {
            handle_button(event);
        }

        // Update current time after potential blocking
        now_ms = get_time_ms();

        // --- Timed tasks (update state, set dirty) ---

        // 1. Status update (1 second interval)
        if (now_ms >= next_status_ms) {
            sys_status_update_basic(&sys_status);
            mark_dirty();
            next_status_ms = now_ms + STATUS_UPDATE_MS;
        }

        // 2. Service list refresh (5 second interval, only on services page)
        if (current_page == PAGE_SERVICES && now_ms >= next_service_ms) {
            sys_status_update_services(&sys_status);
            last_service_refresh = time(NULL);
            mark_dirty();
            next_service_ms = now_ms + SERVICE_REFRESH_INTERVAL * 1000;
        }

        // 3. Menu timeout
        if (menu_active && now_ms >= menu_last_input_ms + MENU_TIMEOUT_MS) {
            menu_exit();
            mark_dirty();
        }

        // --- Rendering (only when needed, at frame deadline) ---
        int need_render = dirty || is_animating();

        if (need_render) {
            // Ensure we render soon if something needs drawing
            if (next_frame_ms > now_ms + FRAME_INTERVAL_ANIM_MS) {
                next_frame_ms = now_ms;  // Render immediately
            }

            if (now_ms >= next_frame_ms) {
                if (display_on) {
                    draw_current_page();
                    // Advance title animation
                    if (advance_title_anim()) {
                        mark_dirty();  // Continue animating next frame
                    }
                }
                dirty = 0;

                // Advance frame time, skip frames if behind (no drift)
                int frame_interval = is_animating() ? FRAME_INTERVAL_ANIM_MS : FRAME_INTERVAL_MS;
                next_frame_ms += frame_interval;
                while (next_frame_ms <= now_ms) {
                    next_frame_ms += frame_interval;  // Catch up, allow frame drop
                }
            }
        } else {
            // Idle: push next_frame far out, rely on timed tasks to wake us
            next_frame_ms = now_ms + 10000;  // Will be clamped by timed task deadlines
        }
    }

    // Cleanup
    syslog(LOG_INFO, "Shutting down...");
    display_shutdown();
    sys_status_cleanup();
    ubus_service_cleanup();
    gpio_button_cleanup();
    closelog();

    return 0;
}

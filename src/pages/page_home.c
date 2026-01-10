/*
 * Home page - displays system status (CPU, memory, uptime)
 */
#include "../page.h"
#include "../sys_status.h"
#include "../fonts.h"
#include "../u8g2_api.h"
#include "../ui_draw.h"

#include <stdio.h>

/* Content Y positions */
#define LINE1_Y (CONTENT_Y_START + 12)
#define LINE2_Y (CONTENT_Y_START + 28)
#define LINE3_Y (CONTENT_Y_START + 44)

static const char *home_get_title(const sys_status_t *status) {
    if (status && status->hostname[0]) {
        return status->hostname;
    }
    return "NanoHat";
}

static void home_render(u8g2_t *u8g2, const sys_status_t *status,
                        page_mode_t mode, uint64_t now_ms, int x_offset) {
    (void)mode;
    (void)now_ms;

    if (!u8g2) return;

    char buf[64];
    int x = MARGIN_LEFT + x_offset;

    u8g2_SetFont(u8g2, font_content);

    if (!status) {
        /* Show placeholder when status not available */
        ui_draw_str(u8g2, x, LINE1_Y, "CPU: --    --" "\xb0" "C");
        ui_draw_str(u8g2, x, LINE2_Y, "MEM:  --");
        ui_draw_str(u8g2, x, LINE3_Y, "RUN:  --");
        return;
    }

    /* Line 1: CPU and Temperature (degree symbol \xb0 = °) */
    snprintf(buf, sizeof(buf), "CPU:%3.0f%%   %2.0f" "\xb0" "C",
             status->cpu_usage, status->cpu_temp);
    ui_draw_str(u8g2, x, LINE1_Y, buf);

    /* Line 2: Memory */
    uint64_t mem_used_mb = (status->mem_total_kb - status->mem_available_kb) / 1024;
    uint64_t mem_total_mb = status->mem_total_kb / 1024;
    snprintf(buf, sizeof(buf), "MEM: %luM / %luM",
             (unsigned long)mem_used_mb, (unsigned long)mem_total_mb);
    ui_draw_str(u8g2, x, LINE2_Y, buf);

    /* Line 3: Runtime (uptime) */
    char uptime_str[16];
    sys_status_format_uptime(status->uptime_sec, uptime_str, sizeof(uptime_str));
    snprintf(buf, sizeof(buf), "RUN: %s", uptime_str);
    ui_draw_str(u8g2, x, LINE3_Y, buf);
}

const page_t page_home = {
    .name = "Home",
    .can_enter = false,
    .init = NULL,
    .destroy = NULL,
    .get_title = home_get_title,
    .render = home_render,
    .on_key = NULL,
    .on_enter = NULL,
    .on_exit = NULL,
};

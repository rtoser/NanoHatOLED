/*
 * Network page - displays IP and traffic stats
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

static const char *network_get_title(const sys_status_t *status) {
    (void)status;
    return "Network";
}

static void network_render(u8g2_t *u8g2, const sys_status_t *status,
                           page_mode_t mode, uint64_t now_ms, int x_offset) {
    (void)mode;
    (void)now_ms;

    if (!u8g2) return;

    char buf[32];
    int x = MARGIN_LEFT + x_offset;

    u8g2_SetFont(u8g2, font_content);

    if (!status) {
        ui_draw_str(u8g2, x, LINE1_Y, "IP: --");
        ui_draw_str(u8g2, x, LINE2_Y, "RX: --");
        ui_draw_str(u8g2, x, LINE3_Y, "TX: --");
        return;
    }

    /* Line 1: IP Address */
    snprintf(buf, sizeof(buf), "IP: %s",
             status->ip_addr[0] ? status->ip_addr : "No IP");
    ui_draw_str(u8g2, x, LINE1_Y, buf);

    /* Line 2: RX */
    char rx_speed[16];
    sys_status_format_speed_bps(status->rx_speed, rx_speed, sizeof(rx_speed));
    snprintf(buf, sizeof(buf), "RX: %s", rx_speed);
    ui_draw_str(u8g2, x, LINE2_Y, buf);

    /* Line 3: TX */
    char tx_speed[16];
    sys_status_format_speed_bps(status->tx_speed, tx_speed, sizeof(tx_speed));
    snprintf(buf, sizeof(buf), "TX: %s", tx_speed);
    ui_draw_str(u8g2, x, LINE3_Y, buf);
}

const page_t page_network = {
    .name = "Network",
    .can_enter = false,
    .init = NULL,
    .destroy = NULL,
    .get_title = network_get_title,
    .render = network_render,
    .on_key = NULL,
    .on_enter = NULL,
    .on_exit = NULL,
};

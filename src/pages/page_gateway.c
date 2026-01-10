/*
 * Gateway page - displays gateway and traffic stats
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

static const char *gateway_get_title(const sys_status_t *status) {
    (void)status;
    return "Gateway";
}

static void gateway_render(u8g2_t *u8g2, const sys_status_t *status,
                           page_mode_t mode, uint64_t now_ms, int x_offset) {
    (void)mode;
    (void)now_ms;

    if (!u8g2) return;

    char buf[32];
    int x = MARGIN_LEFT + x_offset;

    u8g2_SetFont(u8g2, font_content);

    if (!status) {
        ui_draw_str(u8g2, x, LINE1_Y, "GW: --");
        ui_draw_str(u8g2, x, LINE2_Y, "RX: --");
        ui_draw_str(u8g2, x, LINE3_Y, "TX: --");
        return;
    }

    /* Line 1: Gateway */
    snprintf(buf, sizeof(buf), "GW: %s",
             status->gateway[0] ? status->gateway : "--");
    ui_draw_str(u8g2, x, LINE1_Y, buf);

    /* Line 2: RX - label left, speed right-aligned */
    char rx_speed[16];
    sys_status_format_speed_bps(status->rx_speed, rx_speed, sizeof(rx_speed));
    ui_draw_str(u8g2, x, LINE2_Y, "RX:");
    int rx_width = u8g2_GetStrWidth(u8g2, rx_speed);
    ui_draw_str(u8g2, x_offset + SCREEN_WIDTH - MARGIN_RIGHT - rx_width, LINE2_Y, rx_speed);

    /* Line 3: TX - label left, speed right-aligned */
    char tx_speed[16];
    sys_status_format_speed_bps(status->tx_speed, tx_speed, sizeof(tx_speed));
    ui_draw_str(u8g2, x, LINE3_Y, "TX:");
    int tx_width = u8g2_GetStrWidth(u8g2, tx_speed);
    ui_draw_str(u8g2, x_offset + SCREEN_WIDTH - MARGIN_RIGHT - tx_width, LINE3_Y, tx_speed);
}

const page_t page_gateway = {
    .name = "Gateway",
    .can_enter = false,
    .init = NULL,
    .destroy = NULL,
    .get_title = gateway_get_title,
    .render = gateway_render,
    .on_key = NULL,
    .on_enter = NULL,
    .on_exit = NULL,
};

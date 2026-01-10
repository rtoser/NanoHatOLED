/*
 * Gateway page - displays gateway and traffic stats
 */
#include "../page.h"
#include "../sys_status.h"
#include "../fonts.h"
#include "../u8g2_api.h"

#include <stdio.h>

/* Content Y positions */
#define LINE1_Y (CONTENT_Y_START + 12)
#define LINE2_Y (CONTENT_Y_START + 28)
#define LINE3_Y (CONTENT_Y_START + 44)

static const char *gateway_get_title(const sys_status_t *status) {
    (void)status;
    return "Gateway";
}

static void gateway_render(u8g2_t *u8g2, const sys_status_t *status, page_mode_t mode, uint64_t now_ms) {
    (void)mode;
    (void)now_ms;

    if (!u8g2) return;

    char buf[32];

    u8g2_SetFont(u8g2, font_content);

    if (!status) {
        u8g2_DrawStr(u8g2, MARGIN_LEFT, LINE1_Y, "GW: --");
        u8g2_DrawStr(u8g2, MARGIN_LEFT, LINE2_Y, "RX: --");
        u8g2_DrawStr(u8g2, MARGIN_LEFT, LINE3_Y, "TX: --");
        return;
    }

    /* Line 1: Gateway */
    snprintf(buf, sizeof(buf), "GW: %s",
             status->gateway[0] ? status->gateway : "--");
    u8g2_DrawStr(u8g2, MARGIN_LEFT, LINE1_Y, buf);

    /* Line 2: RX */
    char rx_speed[16];
    sys_status_format_speed_bps(status->rx_speed, rx_speed, sizeof(rx_speed));
    snprintf(buf, sizeof(buf), "RX: %s", rx_speed);
    u8g2_DrawStr(u8g2, MARGIN_LEFT, LINE2_Y, buf);

    /* Line 3: TX */
    char tx_speed[16];
    sys_status_format_speed_bps(status->tx_speed, tx_speed, sizeof(tx_speed));
    snprintf(buf, sizeof(buf), "TX: %s", tx_speed);
    u8g2_DrawStr(u8g2, MARGIN_LEFT, LINE3_Y, buf);
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

/*
 * Network page - displays IP, gateway, and traffic stats
 */
#include "../page.h"
#include "../sys_status.h"

#include <stdio.h>
#include <string.h>

/* u8g2 functions */
extern void u8g2_SetFont(u8g2_t *u8g2, const void *font);
extern void u8g2_DrawStr(u8g2_t *u8g2, int x, int y, const char *str);

/* Font references */
extern const void *font_content;

/* Content Y positions */
#define LINE1_Y (CONTENT_Y_START + 12)
#define LINE2_Y (CONTENT_Y_START + 28)
#define LINE3_Y (CONTENT_Y_START + 44)

static void format_speed_bps(uint64_t bytes_per_sec, char *buf, size_t buflen) {
    /* Convert bytes to bits */
    uint64_t bits_per_sec = bytes_per_sec * 8;

    if (bits_per_sec >= 1000000) {
        snprintf(buf, buflen, "%.1fMb/s", (double)bits_per_sec / 1000000.0);
    } else if (bits_per_sec >= 1000) {
        snprintf(buf, buflen, "%.1fKb/s", (double)bits_per_sec / 1000.0);
    } else {
        snprintf(buf, buflen, "%lub/s", (unsigned long)bits_per_sec);
    }
}

static const char *network_get_title(const sys_status_t *status) {
    (void)status;
    return "Network";
}

static void network_render(u8g2_t *u8g2, const sys_status_t *status, page_mode_t mode) {
    (void)mode;

    if (!u8g2) return;

    char buf[32];

    u8g2_SetFont(u8g2, font_content);

    if (!status) {
        u8g2_DrawStr(u8g2, MARGIN_LEFT, LINE1_Y, "IP: --");
        u8g2_DrawStr(u8g2, MARGIN_LEFT, LINE2_Y, "RX: --");
        u8g2_DrawStr(u8g2, MARGIN_LEFT, LINE3_Y, "TX: --");
        return;
    }

    /* Line 1: IP Address */
    snprintf(buf, sizeof(buf), "IP: %s",
             status->ip_addr[0] ? status->ip_addr : "No IP");
    u8g2_DrawStr(u8g2, MARGIN_LEFT, LINE1_Y, buf);

    /* Line 2: RX Speed */
    char rx_speed[16];
    format_speed_bps(status->rx_speed, rx_speed, sizeof(rx_speed));
    snprintf(buf, sizeof(buf), "RX: %s", rx_speed);
    u8g2_DrawStr(u8g2, MARGIN_LEFT, LINE2_Y, buf);

    /* Line 3: TX Speed */
    char tx_speed[16];
    format_speed_bps(status->tx_speed, tx_speed, sizeof(tx_speed));
    snprintf(buf, sizeof(buf), "TX: %s", tx_speed);
    u8g2_DrawStr(u8g2, MARGIN_LEFT, LINE3_Y, buf);
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

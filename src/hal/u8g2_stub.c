/*
 * u8g2 stub implementations for host testing
 */
#include "u8g2_stub.h"
#include "display_hal.h"

#include <stddef.h>

void u8g2_SetFont(u8g2_t *u8g2, const void *font) {
    if (u8g2) u8g2->current_font = font;
}

void u8g2_DrawStr(u8g2_t *u8g2, int x, int y, const char *str) {
    (void)u8g2; (void)x; (void)y; (void)str;
}

void u8g2_DrawHLine(u8g2_t *u8g2, int x, int y, int w) {
    (void)u8g2; (void)x; (void)y; (void)w;
}

void u8g2_DrawBox(u8g2_t *u8g2, int x, int y, int w, int h) {
    (void)u8g2; (void)x; (void)y; (void)w; (void)h;
}

void u8g2_SetDrawColor(u8g2_t *u8g2, int color) {
    if (u8g2) u8g2->draw_color = color;
}

int u8g2_GetStrWidth(u8g2_t *u8g2, const char *str) {
    (void)u8g2; (void)str;
    return 0;
}

u8g2_uint_t u8g2_DrawUTF8(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, const char *str) {
    (void)u8g2; (void)x; (void)y; (void)str;
    return 0;
}

void u8g2_SetClipWindow(u8g2_t *u8g2, int x0, int y0, int x1, int y1) {
    if (u8g2) {
        u8g2->clip_x0 = x0;
        u8g2->clip_y0 = y0;
        u8g2->clip_x1 = x1;
        u8g2->clip_y1 = y1;
    }
}

void u8g2_SetMaxClipWindow(u8g2_t *u8g2) {
    u8g2_SetClipWindow(u8g2, 0, 0, DISPLAY_WIDTH, DISPLAY_HEIGHT);
}

void u8g2_ClearBuffer(u8g2_t *u8g2) {
    (void)u8g2;
}

void u8g2_SendBuffer(u8g2_t *u8g2) {
    (void)u8g2;
}

void u8g2_SetPowerSave(u8g2_t *u8g2, int is_enable) {
    (void)u8g2; (void)is_enable;
}

/* Font placeholders */
const uint8_t *font_title = NULL;
const uint8_t *font_small = NULL;
const uint8_t *font_content = NULL;
const uint8_t *font_symbols = NULL;

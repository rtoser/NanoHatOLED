/*
 * u8g2 stub implementation for host testing
 */
#include "u8g2_stub.h"

#include <string.h>
#include <stdio.h>

/* Font placeholders */
static const uint8_t _font_title = 12;
static const uint8_t _font_small = 8;
static const uint8_t _font_content = 10;

const uint8_t *font_title = &_font_title;
const uint8_t *font_small = &_font_small;
const uint8_t *font_content = &_font_content;

void u8g2_SetFont(u8g2_t *u8g2, const void *font) {
    if (u8g2) {
        u8g2->current_font = font;
    }
}

void u8g2_DrawStr(u8g2_t *u8g2, int x, int y, const char *str) {
    (void)u8g2;
    (void)x;
    (void)y;
    (void)str;
    /* Stub - no actual drawing */
}

void u8g2_DrawHLine(u8g2_t *u8g2, int x, int y, int w) {
    (void)u8g2;
    (void)x;
    (void)y;
    (void)w;
}

void u8g2_DrawBox(u8g2_t *u8g2, int x, int y, int w, int h) {
    (void)u8g2;
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

void u8g2_SetDrawColor(u8g2_t *u8g2, int color) {
    if (u8g2) {
        u8g2->draw_color = color;
    }
}

int u8g2_GetStrWidth(u8g2_t *u8g2, const char *str) {
    if (!str) return 0;

    /* Approximate width based on font */
    int char_width = 6;  /* Default */
    if (u8g2 && u8g2->current_font) {
        /* Use font hint */
        const uint8_t *font_hint = (const uint8_t *)u8g2->current_font;
        if (*font_hint == 12) char_width = 7;
        else if (*font_hint == 10) char_width = 6;
        else if (*font_hint == 8) char_width = 5;
    }

    return (int)strlen(str) * char_width;
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
    if (u8g2) {
        u8g2->clip_x0 = 0;
        u8g2->clip_y0 = 0;
        u8g2->clip_x1 = 128;
        u8g2->clip_y1 = 64;
    }
}

void u8g2_ClearBuffer(u8g2_t *u8g2) {
    (void)u8g2;
}

void u8g2_SendBuffer(u8g2_t *u8g2) {
    (void)u8g2;
}

void u8g2_SetPowerSave(u8g2_t *u8g2, int is_enable) {
    (void)u8g2;
    (void)is_enable;
}

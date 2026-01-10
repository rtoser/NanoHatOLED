/*
 * UI draw helpers with signed x support (for slide animations).
 */
#include "ui_draw.h"
#include "u8g2_api.h"

void ui_draw_str(u8g2_t *u8g2, int x, int y, const char *s) {
    if (!u8g2 || !s) return;

    int width = u8g2_GetStrWidth(u8g2, s);
    if (x >= SCREEN_WIDTH || x + width <= 0) return;

    if (x >= 0) {
        u8g2_DrawStr(u8g2, (u8g2_uint_t)x, (u8g2_uint_t)y, s);
        return;
    }

    int char_w = u8g2_GetStrWidth(u8g2, "0");
    if (char_w <= 0) return;

    int skip = (-x) / char_w;
    int draw_x = x + skip * char_w;
    const char *p = s + skip;
    if (!*p) return;

    if (draw_x < 0) draw_x = 0;
    u8g2_DrawStr(u8g2, (u8g2_uint_t)draw_x, (u8g2_uint_t)y, p);
}

void ui_draw_utf8(u8g2_t *u8g2, int x, int y, const char *s) {
    if (!u8g2 || !s) return;

    int width = u8g2_GetStrWidth(u8g2, s);
    if (x >= SCREEN_WIDTH || x + width <= 0) return;

    if (x < 0) x = 0;
    u8g2_DrawUTF8(u8g2, (u8g2_uint_t)x, (u8g2_uint_t)y, s);
}

void ui_draw_box(u8g2_t *u8g2, int x, int y, int w, int h) {
    if (!u8g2 || w <= 0 || h <= 0) return;

    int x0 = x;
    int x1 = x + w;
    if (x1 <= 0 || x0 >= SCREEN_WIDTH) return;

    if (x0 < 0) {
        w += x0;
        x0 = 0;
    }
    if (x0 + w > SCREEN_WIDTH) {
        w = SCREEN_WIDTH - x0;
    }
    if (w <= 0) return;

    u8g2_DrawBox(u8g2, (u8g2_uint_t)x0, (u8g2_uint_t)y,
                 (u8g2_uint_t)w, (u8g2_uint_t)h);
}

void ui_draw_hline(u8g2_t *u8g2, int x, int y, int w) {
    if (!u8g2 || w <= 0) return;

    int x0 = x;
    int x1 = x + w;
    if (x1 <= 0 || x0 >= SCREEN_WIDTH) return;

    if (x0 < 0) {
        w += x0;
        x0 = 0;
    }
    if (x0 + w > SCREEN_WIDTH) {
        w = SCREEN_WIDTH - x0;
    }
    if (w <= 0) return;

    u8g2_DrawHLine(u8g2, (u8g2_uint_t)x0, (u8g2_uint_t)y, (u8g2_uint_t)w);
}

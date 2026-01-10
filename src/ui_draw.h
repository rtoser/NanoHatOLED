/*
 * UI draw helpers with signed x support (for slide animations).
 */
#ifndef UI_DRAW_H
#define UI_DRAW_H

#include "page.h"

void ui_draw_str(u8g2_t *u8g2, int x, int y, const char *s);
void ui_draw_utf8(u8g2_t *u8g2, int x, int y, const char *s);
void ui_draw_box(u8g2_t *u8g2, int x, int y, int w, int h);
void ui_draw_hline(u8g2_t *u8g2, int x, int y, int w);

#endif

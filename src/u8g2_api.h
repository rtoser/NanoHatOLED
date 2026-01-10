/*
 * u8g2 API declarations used by UI modules
 */
#ifndef U8G2_API_H
#define U8G2_API_H

#include "page.h"

void u8g2_SetFont(u8g2_t *u8g2, const void *font);
void u8g2_DrawStr(u8g2_t *u8g2, int x, int y, const char *str);
void u8g2_DrawHLine(u8g2_t *u8g2, int x, int y, int w);
void u8g2_DrawBox(u8g2_t *u8g2, int x, int y, int w, int h);
void u8g2_SetDrawColor(u8g2_t *u8g2, int color);
int u8g2_GetStrWidth(u8g2_t *u8g2, const char *str);
u8g2_uint_t u8g2_DrawUTF8(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, const char *str);
void u8g2_SetClipWindow(u8g2_t *u8g2, int x0, int y0, int x1, int y1);
void u8g2_SetMaxClipWindow(u8g2_t *u8g2);

#endif

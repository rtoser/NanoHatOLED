/*
 * u8g2 stub for host testing
 *
 * Provides minimal u8g2 API for compilation and testing.
 * On target, real u8g2 library is used.
 */
#ifndef U8G2_STUB_H
#define U8G2_STUB_H

#include <stdint.h>

/* Minimal u8g2 structure */
typedef struct u8g2_struct {
    int draw_color;
    int clip_x0, clip_y0, clip_x1, clip_y1;
    const void *current_font;
} u8g2_t;

typedef uint16_t u8g2_uint_t;

/* Font placeholder type */
typedef uint8_t u8g2_font_t;

/* u8g2 API stubs */
void u8g2_SetFont(u8g2_t *u8g2, const void *font);
void u8g2_DrawStr(u8g2_t *u8g2, int x, int y, const char *str);
void u8g2_DrawHLine(u8g2_t *u8g2, int x, int y, int w);
void u8g2_DrawBox(u8g2_t *u8g2, int x, int y, int w, int h);
void u8g2_DrawFrame(u8g2_t *u8g2, int x, int y, int w, int h);
void u8g2_SetDrawColor(u8g2_t *u8g2, int color);
int u8g2_GetStrWidth(u8g2_t *u8g2, const char *str);
u8g2_uint_t u8g2_DrawUTF8(u8g2_t *u8g2, u8g2_uint_t x, u8g2_uint_t y, const char *str);
void u8g2_SetClipWindow(u8g2_t *u8g2, int x0, int y0, int x1, int y1);
void u8g2_SetMaxClipWindow(u8g2_t *u8g2);
void u8g2_ClearBuffer(u8g2_t *u8g2);
void u8g2_SendBuffer(u8g2_t *u8g2);
void u8g2_SetPowerSave(u8g2_t *u8g2, int is_enable);

/* Font variables (placeholders) */
extern const uint8_t *font_title;
extern const uint8_t *font_small;
extern const uint8_t *font_content;
extern const uint8_t *font_symbols;

#endif

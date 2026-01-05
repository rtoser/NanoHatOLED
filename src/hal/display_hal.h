#ifndef DISPLAY_HAL_H
#define DISPLAY_HAL_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int  (*init)(void);
    void (*cleanup)(void);
    void (*set_power)(bool on);
    void (*begin_frame)(void);
    void (*end_frame)(void);
    void (*draw_text)(int x, int y, const char *text);
    void (*draw_hline)(int x, int y, int w);
    void (*draw_box)(int x, int y, int w, int h);
    void (*set_font)(const char *font_id);
} display_hal_ops_t;

extern const display_hal_ops_t *display_hal;

#endif

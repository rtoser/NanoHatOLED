#include "display_hal.h"

static int null_init(void) {
    return 0;
}

static void null_cleanup(void) {
}

static void null_set_power(bool on) {
    (void)on;
}

static void null_begin_frame(void) {
}

static void null_end_frame(void) {
}

static void null_draw_text(int x, int y, const char *text) {
    (void)x;
    (void)y;
    (void)text;
}

static void null_draw_hline(int x, int y, int w) {
    (void)x;
    (void)y;
    (void)w;
}

static void null_draw_box(int x, int y, int w, int h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

static void null_set_font(const char *font_id) {
    (void)font_id;
}

static const display_hal_ops_t g_display_null_ops = {
    .init = null_init,
    .cleanup = null_cleanup,
    .set_power = null_set_power,
    .begin_frame = null_begin_frame,
    .end_frame = null_end_frame,
    .draw_text = null_draw_text,
    .draw_hline = null_draw_hline,
    .draw_box = null_draw_box,
    .set_font = null_set_font
};

const display_hal_ops_t *display_hal = &g_display_null_ops;

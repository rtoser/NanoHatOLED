#include "display_mock.h"

#include <string.h>

#include "hal/display_hal.h"

static uint32_t g_begin_count = 0;
static uint32_t g_end_count = 0;
static uint32_t g_draw_count = 0;
static char g_last_text[64];

static int mock_init(void) {
    return 0;
}

static void mock_cleanup(void) {
}

static void mock_set_power(bool on) {
    (void)on;
}

static void mock_begin_frame(void) {
    g_begin_count++;
}

static void mock_end_frame(void) {
    g_end_count++;
}

static void mock_draw_text(int x, int y, const char *text) {
    (void)x;
    (void)y;
    g_draw_count++;
    if (text) {
        strncpy(g_last_text, text, sizeof(g_last_text) - 1);
        g_last_text[sizeof(g_last_text) - 1] = '\0';
    }
}

static void mock_draw_hline(int x, int y, int w) {
    (void)x;
    (void)y;
    (void)w;
}

static void mock_draw_box(int x, int y, int w, int h) {
    (void)x;
    (void)y;
    (void)w;
    (void)h;
}

static void mock_set_font(const char *font_id) {
    (void)font_id;
}

static const display_hal_ops_t g_display_mock_ops = {
    .init = mock_init,
    .cleanup = mock_cleanup,
    .set_power = mock_set_power,
    .begin_frame = mock_begin_frame,
    .end_frame = mock_end_frame,
    .draw_text = mock_draw_text,
    .draw_hline = mock_draw_hline,
    .draw_box = mock_draw_box,
    .set_font = mock_set_font
};

const display_hal_ops_t *display_hal = &g_display_mock_ops;

void display_mock_reset(void) {
    g_begin_count = 0;
    g_end_count = 0;
    g_draw_count = 0;
    g_last_text[0] = '\0';
}

uint32_t display_mock_begin_count(void) {
    return g_begin_count;
}

uint32_t display_mock_end_count(void) {
    return g_end_count;
}

uint32_t display_mock_draw_count(void) {
    return g_draw_count;
}

const char *display_mock_last_text(void) {
    return g_last_text;
}

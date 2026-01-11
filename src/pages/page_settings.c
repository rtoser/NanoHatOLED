/*
 * Settings page - system settings configuration
 */
#include "page_settings.h"
#include "../page.h"
#include "../page_controller.h"
#include "../hal/display_hal.h"
#include "../sys_status.h"
#include "../fonts.h"
#include "../u8g2_api.h"
#include "../ui_draw.h"
#include <stdio.h>
#include <string.h>

/* Content Y positions */
#define LINE1_Y (CONTENT_Y_START + 12)
#define LINE2_Y (CONTENT_Y_START + 28)

/* Settings item count */
#define SETTINGS_COUNT 2

/* Setting indices */
#define SETTING_AUTO_SLEEP  0
#define SETTING_BRIGHTNESS  1

/* Value column X position (right-aligned reference point) */
#define VALUE_X (SCREEN_WIDTH - MARGIN_RIGHT)

/* Page state */
static struct {
    int selected_index;
    uint8_t brightness;  /* 1-10 */
} state;

static void settings_init(void) {
    memset(&state, 0, sizeof(state));
    state.brightness = 7;  /* Default brightness */

    /* Sync initial brightness to hardware */
    if (display_hal && display_hal->set_contrast) {
        display_hal->set_contrast(state.brightness);
    }
}

static const char *settings_get_title(const sys_status_t *status) {
    (void)status;
    return "Settings";
}

static void render_setting_line(u8g2_t *u8g2, int y, const char *name,
                                 const char *value, int is_selected,
                                 page_mode_t mode, int x_offset) {
    if (is_selected && mode == PAGE_MODE_ENTER) {
        /* Draw inverted background */
        u8g2_SetDrawColor(u8g2, 1);
        ui_draw_box(u8g2, x_offset, y - 12, SCREEN_WIDTH, 14);
        u8g2_SetDrawColor(u8g2, 0);
    }

    /* Draw setting name */
    u8g2_SetFont(u8g2, font_content);
    ui_draw_str(u8g2, MARGIN_LEFT + x_offset, y, name);

    /* Draw value (right-aligned, same font for alignment) */
    int value_width = u8g2_GetStrWidth(u8g2, value);
    ui_draw_str(u8g2, x_offset + VALUE_X - value_width, y, value);

    /* Restore draw color */
    if (is_selected && mode == PAGE_MODE_ENTER) {
        u8g2_SetDrawColor(u8g2, 1);
    }
}

static void settings_render(u8g2_t *u8g2, const sys_status_t *status,
                            page_mode_t mode, uint64_t now_ms, int x_offset) {
    (void)status;
    (void)now_ms;

    if (!u8g2) return;

    int y_positions[] = {LINE1_Y, LINE2_Y};
    char value_buf[8];

    /* Auto Sleep setting */
    bool auto_sleep = page_controller_is_auto_screen_off_enabled();
    render_setting_line(u8g2, y_positions[SETTING_AUTO_SLEEP], "Auto Sleep",
                        auto_sleep ? "ON" : "OFF",
                        state.selected_index == SETTING_AUTO_SLEEP, mode, x_offset);

    /* Brightness setting */
    snprintf(value_buf, sizeof(value_buf), "%d", state.brightness);
    render_setting_line(u8g2, y_positions[SETTING_BRIGHTNESS], "Brightness",
                        value_buf, state.selected_index == SETTING_BRIGHTNESS,
                        mode, x_offset);
}

static bool settings_on_key(uint8_t key, bool long_press, page_mode_t mode) {
    if (mode != PAGE_MODE_ENTER) {
        return false;
    }

    switch (key) {
        case KEY_K1:
            if (!long_press) {
                /* Move up (with wrap) */
                state.selected_index--;
                if (state.selected_index < 0) {
                    state.selected_index = SETTINGS_COUNT - 1;
                }
                return true;
            }
            break;

        case KEY_K3:
            if (!long_press) {
                /* Move down (with wrap) */
                state.selected_index++;
                if (state.selected_index >= SETTINGS_COUNT) {
                    state.selected_index = 0;
                }
                return true;
            }
            break;

        case KEY_K2:
            if (!long_press) {
                /* Toggle/cycle current setting */
                if (state.selected_index == SETTING_AUTO_SLEEP) {
                    /* Toggle auto sleep */
                    bool current = page_controller_is_auto_screen_off_enabled();
                    page_controller_set_auto_screen_off(!current);
                } else if (state.selected_index == SETTING_BRIGHTNESS) {
                    /* Cycle brightness 1-10 */
                    state.brightness++;
                    if (state.brightness > 10) {
                        state.brightness = 1;
                    }
                    /* Apply brightness */
                    if (display_hal && display_hal->set_contrast) {
                        display_hal->set_contrast(state.brightness);
                    }
                }
                return true;
            }
            break;
    }

    return false;
}

static void settings_on_enter(void) {
    /* Reset selection to first item when entering */
    state.selected_index = 0;
}

static void settings_on_exit(void) {
    /* Nothing to clean up */
}

static int settings_get_selected_index(void) {
    return state.selected_index;
}

static int settings_get_item_count(void) {
    return SETTINGS_COUNT;
}

const page_t page_settings = {
    .name = "Settings",
    .can_enter = true,
    .init = settings_init,
    .destroy = NULL,
    .get_title = settings_get_title,
    .render = settings_render,
    .on_key = settings_on_key,
    .on_enter = settings_on_enter,
    .on_exit = settings_on_exit,
    .get_selected_index = settings_get_selected_index,
    .get_item_count = settings_get_item_count,
};

/*
 * Services page - displays and controls monitored services
 */
#include "../page.h"
#include "../sys_status.h"
#include "../anim.h"
#include "../fonts.h"
#include "../u8g2_api.h"
#include "../ui_draw.h"
#include <stdio.h>
#include <string.h>

/* Content Y positions */
#define LINE1_Y (CONTENT_Y_START + 12)
#define LINE2_Y (CONTENT_Y_START + 28)
#define LINE3_Y (CONTENT_Y_START + 44)

/* Service status icons */
#define ICON_RUNNING "\xE2\x96\xB6"  /* ▶ */
#define ICON_STOPPED "\xE2\x96\xA0"  /* ■ */
#define ICON_PENDING "..."           /* Query in progress */
#define ICON_UNKNOWN "--"            /* Timeout/error */

/* Maximum visible services */
#define VISIBLE_LINES 3

/* Service UI states */
typedef enum {
    SVC_UI_STOPPED,
    SVC_UI_RUNNING,
    SVC_UI_STARTING,
    SVC_UI_STOPPING,
    SVC_UI_ERROR,
} svc_ui_state_t;

/* Page state */
static struct {
    int selected_index;
    int scroll_offset;
    svc_ui_state_t ui_states[MAX_SERVICES];
    uint64_t state_change_ms[MAX_SERVICES];
} state;

static void services_init(void) {
    memset(&state, 0, sizeof(state));
}

static const char *services_get_title(const sys_status_t *status) {
    (void)status;
    return "Services";
}

static const char *get_service_icon(const service_status_t *svc, svc_ui_state_t ui_state,
                                    uint64_t now_ms, int svc_idx) {
    /* Handle transitional states with blinking */
    if (ui_state == SVC_UI_STARTING || ui_state == SVC_UI_STOPPING) {
        uint64_t elapsed = now_ms - state.state_change_ms[svc_idx];
        int phase = (int)(elapsed / ANIM_BLINK_PERIOD_MS) % 2;
        if (phase == 0) {
            return (ui_state == SVC_UI_STARTING) ? ICON_RUNNING : ICON_STOPPED;
        } else {
            return " ";  /* Blink off */
        }
    }

    if (ui_state == SVC_UI_ERROR) {
        return ICON_UNKNOWN;
    }

    /* Query in progress - show pending indicator */
    if (svc->query_pending) {
        return ICON_PENDING;
    }

    /* Query completed but failed (timeout/error) */
    if (!svc->status_valid) {
        return ICON_UNKNOWN;
    }

    return svc->running ? ICON_RUNNING : ICON_STOPPED;
}

static void render_service_line(u8g2_t *u8g2, int y, const char *name,
                                const char *icon, int is_selected, page_mode_t mode,
                                int x_offset) {
    char buf[32];

    if (is_selected && mode == PAGE_MODE_ENTER) {
        /* Draw inverted background */
        u8g2_SetDrawColor(u8g2, 1);
        ui_draw_box(u8g2, x_offset, y - 12, SCREEN_WIDTH, 14);
        u8g2_SetDrawColor(u8g2, 0);
    }

    /* Draw service name */
    u8g2_SetFont(u8g2, font_content);
    snprintf(buf, sizeof(buf), "%s", name);
    ui_draw_str(u8g2, MARGIN_LEFT + x_offset, y, buf);

    /* Draw status icon (right-aligned) */
    u8g2_SetFont(u8g2, font_symbols);
    int icon_width = u8g2_GetStrWidth(u8g2, icon);
    ui_draw_utf8(u8g2, x_offset + SCREEN_WIDTH - MARGIN_RIGHT - icon_width, y, icon);

    /* Restore draw color */
    if (is_selected && mode == PAGE_MODE_ENTER) {
        u8g2_SetDrawColor(u8g2, 1);
    }
}

static void services_render(u8g2_t *u8g2, const sys_status_t *status,
                            page_mode_t mode, uint64_t now_ms, int x_offset) {
    if (!u8g2) return;

    if (!status) {
        u8g2_SetFont(u8g2, font_content);
        ui_draw_str(u8g2, MARGIN_LEFT + x_offset, LINE2_Y, "Loading...");
        return;
    }

    int y_positions[] = {LINE1_Y, LINE2_Y, LINE3_Y};
    int service_count = (int)status->service_count;

    if (service_count == 0) {
        u8g2_SetFont(u8g2, font_content);
        ui_draw_str(u8g2, MARGIN_LEFT + x_offset, LINE2_Y, "No services");
        return;
    }

    /* Render visible services */
    for (int i = 0; i < VISIBLE_LINES && (state.scroll_offset + i) < service_count; i++) {
        int svc_idx = state.scroll_offset + i;
        const service_status_t *svc = &status->services[svc_idx];

        const char *icon = get_service_icon(svc, state.ui_states[svc_idx], now_ms, svc_idx);
        int is_selected = (svc_idx == state.selected_index);

        render_service_line(u8g2, y_positions[i], svc->name, icon, is_selected, mode, x_offset);
    }

    /* Draw scroll indicators if needed */
    if (mode == PAGE_MODE_ENTER && service_count > VISIBLE_LINES) {
        u8g2_SetFont(u8g2, font_symbols);

        if (state.scroll_offset > 0) {
            /* Up arrow in title bar area */
            ui_draw_utf8(u8g2, x_offset + SCREEN_WIDTH - 10, 12, "\xE2\x96\xB2");
        }

        if (state.scroll_offset + VISIBLE_LINES < service_count) {
            /* Down arrow at bottom */
            ui_draw_utf8(u8g2, x_offset + SCREEN_WIDTH - 10, SCREEN_HEIGHT - 2, "\xE2\x96\xBC");
        }
    }
}

static void ensure_visible(int service_count) {
    /* Adjust scroll to keep selected item visible */
    if (state.selected_index < state.scroll_offset) {
        state.scroll_offset = state.selected_index;
    } else if (state.selected_index >= state.scroll_offset + VISIBLE_LINES) {
        state.scroll_offset = state.selected_index - VISIBLE_LINES + 1;
    }

    /* Clamp scroll offset */
    int max_scroll = service_count - VISIBLE_LINES;
    if (max_scroll < 0) max_scroll = 0;
    if (state.scroll_offset > max_scroll) {
        state.scroll_offset = max_scroll;
    }
    if (state.scroll_offset < 0) {
        state.scroll_offset = 0;
    }
}

static bool services_on_key(uint8_t key, bool long_press, page_mode_t mode) {
    if (mode != PAGE_MODE_ENTER) {
        return false;
    }

    /* In enter mode - handle navigation and toggle */
    const service_config_t *cfg = service_config_get();
    int service_count = cfg ? (int)cfg->count : 0;

    if (service_count == 0) {
        return false;
    }

    switch (key) {
        case KEY_K1:
            if (!long_press) {
                /* Move up (with wrap) */
                state.selected_index--;
                if (state.selected_index < 0) {
                    state.selected_index = service_count - 1;
                }
                ensure_visible(service_count);
                return true;
            }
            break;

        case KEY_K3:
            if (!long_press) {
                /* Move down (with wrap) */
                state.selected_index++;
                if (state.selected_index >= service_count) {
                    state.selected_index = 0;
                }
                ensure_visible(service_count);
                return true;
            }
            break;

        case KEY_K2:
            /* Service control will be implemented in Phase 4 (ubus async). */
            break;
    }

    return false;
}

static void services_on_enter(void) {
    /* Reset selection to first item when entering */
    state.selected_index = 0;
    state.scroll_offset = 0;
}

static void services_on_exit(void) {
    /* Nothing special needed */
}

const page_t page_services = {
    .name = "Services",
    .can_enter = true,
    .init = services_init,
    .destroy = NULL,
    .get_title = services_get_title,
    .render = services_render,
    .on_key = services_on_key,
    .on_enter = services_on_enter,
    .on_exit = services_on_exit,
};

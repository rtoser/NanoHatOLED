/*
 * Services page - displays and controls monitored services
 */
#include "page_services.h"
#include "../page.h"
#include "../sys_status.h"
#include "../anim.h"
#include "../fonts.h"
#include "../u8g2_api.h"
#include "../ui_draw.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Content Y positions */
#define LINE1_Y (CONTENT_Y_START + 12)
#define LINE2_Y (CONTENT_Y_START + 28)
#define LINE3_Y (CONTENT_Y_START + 44)

/* Service status icons */
#define ICON_RUNNING "\xE2\x96\xB6"  /* ▶ */
#define ICON_STOPPED "\xE2\x96\xA0"  /* ■ */
#define ICON_PENDING_RUN "\xE2\x96\xB7"  /* ▷ Query in progress (was running) */
#define ICON_PENDING_STOP "\xE2\x96\xA1" /* □ Query in progress (was stopped) */
#define ICON_UNKNOWN "--"            /* Timeout/error */

/* Maximum visible services */
#define VISIBLE_LINES 3

/* Dialog box dimensions */
#define DIALOG_WIDTH  100
#define DIALOG_HEIGHT 40
#define DIALOG_X      ((SCREEN_WIDTH - DIALOG_WIDTH) / 2)
#define DIALOG_Y      ((SCREEN_HEIGHT - DIALOG_HEIGHT) / 2)

/* Service UI states */
typedef enum {
    SVC_UI_STOPPED,
    SVC_UI_RUNNING,
    SVC_UI_STARTING,
    SVC_UI_STOPPING,
    SVC_UI_ERROR,
} svc_ui_state_t;

/* Dialog state */
typedef enum {
    DIALOG_NONE,
    DIALOG_CONFIRM,
} dialog_state_t;

/* Page state */
static struct {
    int selected_index;
    int scroll_offset;
    svc_ui_state_t ui_states[MAX_SERVICES];
    uint64_t state_change_ms[MAX_SERVICES];
    /* Cached running state from last render (for on_key) */
    bool cached_running[MAX_SERVICES];
    /* Dialog state */
    dialog_state_t dialog;
    int dialog_selection;  /* 0=No, 1=Yes */
    /* Pending control operation */
    int pending_control_index;  /* -1 = none */
    bool pending_control_start;
} state;

static void services_init(void) {
    memset(&state, 0, sizeof(state));
    state.pending_control_index = -1;
}

/*
 * Render confirmation dialog overlay
 */
static void render_dialog(u8g2_t *u8g2, const char *service_name, bool is_running, int x_offset) {
    int dx = DIALOG_X + x_offset;
    int dy = DIALOG_Y;

    /* Draw dialog background (clear area) */
    u8g2_SetDrawColor(u8g2, 0);
    ui_draw_box(u8g2, dx, dy, DIALOG_WIDTH, DIALOG_HEIGHT);

    /* Draw dialog border */
    u8g2_SetDrawColor(u8g2, 1);
    u8g2_DrawFrame(u8g2, dx, dy, DIALOG_WIDTH, DIALOG_HEIGHT);

    /* Draw action text */
    u8g2_SetFont(u8g2, font_content);
    const char *action = is_running ? "Stop" : "Start";
    char title[24];
    snprintf(title, sizeof(title), "%s %s?", action, service_name);

    /* Truncate if too long */
    int title_w = u8g2_GetStrWidth(u8g2, title);
    if (title_w > DIALOG_WIDTH - 8) {
        snprintf(title, sizeof(title), "%s svc?", action);
    }
    int title_x = dx + (DIALOG_WIDTH - u8g2_GetStrWidth(u8g2, title)) / 2;
    ui_draw_str(u8g2, title_x, dy + 14, title);

    /* Draw No/Yes buttons */
    int btn_y = dy + DIALOG_HEIGHT - 10;
    int no_x = dx + 15;
    int yes_x = dx + DIALOG_WIDTH - 35;

    /* Highlight selected button */
    if (state.dialog_selection == 0) {
        /* No selected - draw inverted */
        ui_draw_box(u8g2, no_x - 2, btn_y - 10, 24, 14);
        u8g2_SetDrawColor(u8g2, 0);
        ui_draw_str(u8g2, no_x, btn_y, "No");
        u8g2_SetDrawColor(u8g2, 1);
        ui_draw_str(u8g2, yes_x, btn_y, "Yes");
    } else {
        /* Yes selected - draw inverted */
        ui_draw_str(u8g2, no_x, btn_y, "No");
        ui_draw_box(u8g2, yes_x - 2, btn_y - 10, 28, 14);
        u8g2_SetDrawColor(u8g2, 0);
        ui_draw_str(u8g2, yes_x, btn_y, "Yes");
        u8g2_SetDrawColor(u8g2, 1);
    }
}

static const char *services_get_title(const sys_status_t *status) {
    (void)status;
    return "Services";
}

static const char *get_service_icon(const service_status_t *svc, svc_ui_state_t ui_state,
                                    uint64_t now_ms, int svc_idx) {
    /* Handle transitional states with blinking (target icon <-> blank) */
    if (ui_state == SVC_UI_STARTING || ui_state == SVC_UI_STOPPING) {
        uint64_t elapsed = now_ms - state.state_change_ms[svc_idx];
        int phase = (int)(elapsed / ANIM_BLINK_PERIOD_MS) % 2;
        /* Target icon based on operation direction */
        const char *target_icon = (ui_state == SVC_UI_STARTING) ? ICON_RUNNING : ICON_STOPPED;
        return (phase == 0) ? target_icon : " ";
    }

    /* Query in progress - show hollow version of current state */
    if (svc->query_pending) {
        return svc->running ? ICON_PENDING_RUN : ICON_PENDING_STOP;
    }

    /* Query completed but failed (timeout/error) */
    if (!svc->status_valid) {
        return ICON_UNKNOWN;
    }

    /* Normal state - based on actual running status */
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

    /* Cache running state and sync ui_states with actual state */
    for (int i = 0; i < service_count && i < MAX_SERVICES; i++) {
        state.cached_running[i] = status->services[i].running;

        /* Clear transition state when actual state matches expectation */
        if (state.ui_states[i] == SVC_UI_STARTING && status->services[i].running) {
            state.ui_states[i] = SVC_UI_RUNNING;
        } else if (state.ui_states[i] == SVC_UI_STOPPING && !status->services[i].running) {
            state.ui_states[i] = SVC_UI_STOPPED;
        }
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

    /* Render dialog overlay if active */
    if (state.dialog == DIALOG_CONFIRM && state.selected_index < service_count) {
        const service_status_t *svc = &status->services[state.selected_index];
        render_dialog(u8g2, svc->name, svc->running, x_offset);
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

    const service_config_t *cfg = service_config_get();
    int service_count = cfg ? (int)cfg->count : 0;

    if (service_count == 0) {
        return false;
    }

    /* Handle dialog input */
    if (state.dialog == DIALOG_CONFIRM) {
        switch (key) {
            case KEY_K1:
            case KEY_K3:
                if (!long_press) {
                    /* Toggle between No(0) and Yes(1) */
                    state.dialog_selection = 1 - state.dialog_selection;
                    return true;
                }
                break;

            case KEY_K2:
                if (!long_press) {
                    /* Confirm selection */
                    if (state.dialog_selection == 1) {
                        /* Yes selected - toggle service based on actual running state */
                        bool is_running = state.cached_running[state.selected_index];
                        state.ui_states[state.selected_index] =
                            is_running ? SVC_UI_STOPPING : SVC_UI_STARTING;
                        /* Schedule control operation (consumed by ui_controller) */
                        state.pending_control_index = state.selected_index;
                        state.pending_control_start = !is_running;
                    }
                    /* Close dialog */
                    state.dialog = DIALOG_NONE;
                    return true;
                }
                break;
        }
        return false;
    }

    /* Normal list navigation */
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
            if (!long_press) {
                /* Open confirmation dialog */
                state.dialog = DIALOG_CONFIRM;
                state.dialog_selection = 0;  /* Default to No */
                return true;
            }
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
    /* Close any open dialog */
    state.dialog = DIALOG_NONE;
    state.pending_control_index = -1;
}

bool page_services_take_control_request(int *index, bool *start, uint64_t now_ms) {
    if (state.pending_control_index < 0) {
        return false;
    }

    int idx = state.pending_control_index;
    state.pending_control_index = -1;

    if (index) *index = idx;
    if (start) *start = state.pending_control_start;
    if (idx >= 0 && idx < MAX_SERVICES) {
        state.state_change_ms[idx] = now_ms;
    }
    return true;
}

void page_services_notify_control_result(int index, bool success) {
    if (index < 0 || index >= MAX_SERVICES) return;

    if (!success) {
        state.ui_states[index] = SVC_UI_ERROR;
    }
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

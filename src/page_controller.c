#include "page_controller.h"
#include "ui_draw.h"

#include <stdio.h>
#include <string.h>

#include "u8g2_api.h"
#include "fonts.h"
/* Default idle timeout: 30 seconds */
#define DEFAULT_IDLE_TIMEOUT_MS 30000

/* Font sizes (u8g2 font height references) */
#define FONT_TITLE_HEIGHT 12
#define FONT_SMALL_HEIGHT 8

/* Title bar layout */
#define TITLE_X       2
#define TITLE_Y       12   /* Baseline for 12px font in 16px title bar */
#define PAGE_IND_X    126  /* Right-aligned page indicator */
#define PAGE_IND_Y    12
#define ENTER_IND_X   108  /* Position for enter indicator */
#define TITLE_LINE_Y  15   /* Horizontal line under title */

/* Content area */
#define CONTENT_Y_LINE1 (CONTENT_Y_START + 12)
#define CONTENT_Y_LINE2 (CONTENT_Y_START + 28)
#define CONTENT_Y_LINE3 (CONTENT_Y_START + 44)

void page_controller_init(page_controller_t *pc, const page_t **pages, int page_count) {
    if (!pc) return;

    memset(pc, 0, sizeof(*pc));
    pc->pages = pages;
    pc->page_count = page_count;
    pc->screen_state = SCREEN_ON;
    pc->current_page = 0;  /* Default to Home page */
    pc->page_mode = PAGE_MODE_VIEW;
    pc->idle_timeout_ms = DEFAULT_IDLE_TIMEOUT_MS;
    pc->anim.type = ANIM_NONE;

    /* Initialize all pages */
    for (int i = 0; i < page_count; i++) {
        if (pages[i] && pages[i]->init) {
            pages[i]->init();
        }
    }
}

void page_controller_destroy(page_controller_t *pc) {
    if (!pc) return;

    /* Destroy all pages */
    for (int i = 0; i < pc->page_count; i++) {
        if (pc->pages[i] && pc->pages[i]->destroy) {
            pc->pages[i]->destroy();
        }
    }
}

static void start_animation(page_controller_t *pc, anim_type_t type, uint64_t now_ms) {
    pc->anim.type = type;
    pc->anim.start_ms = now_ms;
}

static void switch_page(page_controller_t *pc, int direction, uint64_t now_ms) {
    if (pc->page_count <= 1) return;
    if (pc->anim.type != ANIM_NONE) return;

    int new_page = pc->current_page + direction;
    if (new_page < 0) new_page = pc->page_count - 1;
    if (new_page >= pc->page_count) new_page = 0;

    pc->anim.from_page = pc->current_page;
    pc->anim.to_page = new_page;
    /* Don't update current_page here - will be updated when animation completes */

    if (direction > 0) {
        start_animation(pc, ANIM_SLIDE_LEFT, now_ms);
    } else {
        start_animation(pc, ANIM_SLIDE_RIGHT, now_ms);
    }
}

static bool try_enter_mode(page_controller_t *pc, uint64_t now_ms) {
    if (pc->page_mode == PAGE_MODE_ENTER) {
        /* Already in enter mode - exit */
        pc->page_mode = PAGE_MODE_VIEW;
        start_animation(pc, ANIM_EXIT_MODE, now_ms);

        const page_t *page = pc->pages[pc->current_page];
        if (page && page->on_exit) {
            page->on_exit();
        }
        return true;
    }

    const page_t *page = pc->pages[pc->current_page];
    if (!page) return false;

    if (page->can_enter) {
        pc->page_mode = PAGE_MODE_ENTER;
        start_animation(pc, ANIM_ENTER_MODE, now_ms);
        if (page->on_enter) {
            page->on_enter();
        }
        return true;
    } else {
        /* Page doesn't support enter - shake title */
        start_animation(pc, ANIM_TITLE_SHAKE, now_ms);
        return true;
    }
}

bool page_controller_handle_key(page_controller_t *pc, uint8_t key, bool long_press, uint64_t now_ms) {
    if (!pc) return false;

    /* Update activity time */
    pc->last_activity_ms = now_ms;

    /* Screen off - any key wakes screen */
    if (pc->screen_state == SCREEN_OFF) {
        pc->screen_state = SCREEN_ON;
        return true;
    }

    /* Don't process keys during animation (except complete it first) */
    if (pc->anim.type != ANIM_NONE && !anim_is_complete(&pc->anim, now_ms)) {
        return false;
    }
    pc->anim.type = ANIM_NONE;

    const page_t *page = pc->pages[pc->current_page];

    /* Let page handle key first in enter mode */
    if (pc->page_mode == PAGE_MODE_ENTER && page && page->on_key) {
        if (key == KEY_K2 && long_press) {
            /* K2 long press always exits enter mode */
            return try_enter_mode(pc, now_ms);
        }
        if (page->on_key(key, long_press, pc->page_mode)) {
            return true;
        }
    }

    /* View mode key handling */
    if (pc->page_mode == PAGE_MODE_VIEW) {
        switch (key) {
            case KEY_K1:
                if (!long_press) {
                    switch_page(pc, -1, now_ms);  /* Previous page */
                    return true;
                }
                break;

            case KEY_K3:
                if (!long_press) {
                    switch_page(pc, 1, now_ms);   /* Next page */
                    return true;
                }
                break;

            case KEY_K2:
                if (long_press) {
                    return try_enter_mode(pc, now_ms);
                }
                /* K2 short press: screen off on any page */
                pc->screen_state = SCREEN_OFF;
                return true;
        }

        /* Let page handle remaining keys */
        if (page && page->on_key) {
            if (page->on_key(key, long_press, pc->page_mode)) {
                return true;
            }
        }
    }

    return false;
}

bool page_controller_tick(page_controller_t *pc, uint64_t now_ms) {
    if (!pc) return false;

    bool needs_render = false;

    /* Initialize last_activity_ms on first tick */
    if (pc->last_activity_ms == 0) {
        pc->last_activity_ms = now_ms;
    }

    /* Check auto screen-off */
    if (pc->screen_state == SCREEN_ON && pc->idle_timeout_ms > 0) {
        if (now_ms - pc->last_activity_ms >= pc->idle_timeout_ms) {
            pc->screen_state = SCREEN_OFF;
            needs_render = true;
        }
    }

    /* Check animation completion */
    if (pc->anim.type != ANIM_NONE) {
        if (anim_is_complete(&pc->anim, now_ms)) {
            /* Update current_page when slide animation completes */
            if (pc->anim.type == ANIM_SLIDE_LEFT || pc->anim.type == ANIM_SLIDE_RIGHT) {
                pc->current_page = pc->anim.to_page;
            }
            pc->anim.type = ANIM_NONE;
        }
        needs_render = true;
    }

    return needs_render;
}

static void render_title_bar(page_controller_t *pc, u8g2_t *u8g2,
                             const sys_status_t *status, int page_idx, int x_offset, uint64_t now_ms) {
    if (page_idx < 0 || page_idx >= pc->page_count) return;
    const page_t *page = pc->pages[page_idx];
    if (!page) return;

    /* Get title */
    const char *title = page->name;
    if (page->get_title) {
        const char *dynamic_title = page->get_title(status);
        if (dynamic_title) title = dynamic_title;
    }

    /* Calculate title position */
    int title_x = TITLE_X + x_offset;

    /* Handle mode transition animation */
    if (pc->anim.type == ANIM_ENTER_MODE || pc->anim.type == ANIM_EXIT_MODE) {
        float progress = anim_progress(pc->anim.start_ms, now_ms, ANIM_MODE_DURATION_MS);
        progress = ease_in_out_quad(progress);

        u8g2_SetFont(u8g2, font_title);
        int title_width = u8g2_GetStrWidth(u8g2, title);
        int center_x = (SCREEN_WIDTH - title_width) / 2;

        if (pc->anim.type == ANIM_ENTER_MODE) {
            title_x = TITLE_X + (int)((center_x - TITLE_X) * progress);
        } else {
            title_x = center_x + (int)((TITLE_X - center_x) * progress);
        }
    } else if (pc->page_mode == PAGE_MODE_ENTER) {
        /* Centered title in enter mode */
        u8g2_SetFont(u8g2, font_title);
        int title_width = u8g2_GetStrWidth(u8g2, title);
        title_x = (SCREEN_WIDTH - title_width) / 2;
    }

    /* Handle shake animation */
    if (pc->anim.type == ANIM_TITLE_SHAKE) {
        float progress = anim_progress(pc->anim.start_ms, now_ms, ANIM_SHAKE_DURATION_MS);
        title_x += anim_shake_offset(progress);
    }

    /* Draw title */
    u8g2_SetFont(u8g2, font_title);
    ui_draw_str(u8g2, title_x, TITLE_Y, title);

    /* Draw page indicator (only in view mode) */
    if (pc->page_mode == PAGE_MODE_VIEW && pc->anim.type != ANIM_ENTER_MODE) {
        char page_ind[32];
        snprintf(page_ind, sizeof(page_ind), "%d/%d", pc->current_page + 1, pc->page_count);
        u8g2_SetFont(u8g2, font_small);
        int ind_width = u8g2_GetStrWidth(u8g2, page_ind);
        ui_draw_str(u8g2, PAGE_IND_X - ind_width + x_offset, PAGE_IND_Y, page_ind);

        /* Draw enter indicator if page supports it */
        if (page->can_enter) {
            u8g2_SetFont(u8g2, font_symbols);
            ui_draw_utf8(u8g2, ENTER_IND_X + x_offset, PAGE_IND_Y, "\xE2\x8F\x8E");
        }
    }

    /* Draw title bar separator line */
    ui_draw_hline(u8g2, x_offset, TITLE_LINE_Y, SCREEN_WIDTH);
}

static void render_page_content(page_controller_t *pc, u8g2_t *u8g2,
                                const sys_status_t *status, int page_idx,
                                int x_offset, uint64_t now_ms) {
    if (page_idx < 0 || page_idx >= pc->page_count) return;

    const page_t *page = pc->pages[page_idx];
    if (!page || !page->render) return;

    /* Set clip window for content area with offset */
    u8g2_SetClipWindow(u8g2, 0, CONTENT_Y_START,
                       SCREEN_WIDTH, SCREEN_HEIGHT);

    page->render(u8g2, status, pc->page_mode, now_ms, x_offset);

    u8g2_SetMaxClipWindow(u8g2);
}

void page_controller_render(page_controller_t *pc, u8g2_t *u8g2,
                           const sys_status_t *status, uint64_t now_ms) {
    if (!pc || !u8g2) return;

    /* Handle slide animation */
    if (pc->anim.type == ANIM_SLIDE_LEFT || pc->anim.type == ANIM_SLIDE_RIGHT) {
        float progress = anim_progress(pc->anim.start_ms, now_ms, ANIM_SLIDE_DURATION_MS);

        int out_offset = anim_slide_offset(progress, pc->anim.type, 1);
        int in_offset = anim_slide_offset(progress, pc->anim.type, 0);

        /* Render outgoing page (current page sliding out) */
        render_title_bar(pc, u8g2, status, pc->anim.from_page, out_offset, now_ms);
        render_page_content(pc, u8g2, status, pc->anim.from_page, out_offset, now_ms);

        /* Render incoming page (new page sliding in) */
        render_title_bar(pc, u8g2, status, pc->anim.to_page, in_offset, now_ms);
        render_page_content(pc, u8g2, status, pc->anim.to_page, in_offset, now_ms);
    } else {
        /* Normal render */
        render_title_bar(pc, u8g2, status, pc->current_page, 0, now_ms);
        render_page_content(pc, u8g2, status, pc->current_page, 0, now_ms);
    }
}

bool page_controller_is_screen_on(const page_controller_t *pc) {
    return pc && pc->screen_state == SCREEN_ON;
}

bool page_controller_is_animating(const page_controller_t *pc) {
    return pc && pc->anim.type != ANIM_NONE;
}

void page_controller_set_idle_timeout(page_controller_t *pc, uint32_t timeout_ms) {
    if (pc) {
        pc->idle_timeout_ms = timeout_ms;
    }
}

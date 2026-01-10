/*
 * Page controller for NanoHat OLED UI
 *
 * Manages pages, screen state, animations, and input handling.
 */
#ifndef PAGE_CONTROLLER_H
#define PAGE_CONTROLLER_H

#include <stdbool.h>
#include <stdint.h>

#include "page.h"
#include "anim.h"
#include "sys_status.h"

/* Screen state */
typedef enum {
    SCREEN_OFF,
    SCREEN_ON,
} screen_state_t;

/* Forward declaration */
struct u8g2_struct;
typedef struct u8g2_struct u8g2_t;

/* Page controller state */
typedef struct {
    /* Screen state */
    screen_state_t screen_state;
    uint64_t last_activity_ms;

    /* Page state */
    int current_page;
    int page_count;
    page_mode_t page_mode;

    /* Animation state */
    anim_state_t anim;

    /* Enter mode tracking */
    uint64_t enter_mode_start_ms;

    /* Configuration */
    uint32_t idle_timeout_ms;
    uint32_t enter_mode_timeout_ms;

    /* Registered pages */
    const page_t **pages;
} page_controller_t;

/*
 * Initialize page controller with page array.
 */
void page_controller_init(page_controller_t *pc, const page_t **pages, int page_count);

/*
 * Cleanup page controller.
 */
void page_controller_destroy(page_controller_t *pc);

/*
 * Handle button event.
 * key: KEY_K1, KEY_K2, or KEY_K3
 * long_press: true for long press
 * now_ms: current time in milliseconds
 * Returns: true if screen state or page changed
 */
bool page_controller_handle_key(page_controller_t *pc, uint8_t key, bool long_press, uint64_t now_ms);

/*
 * Handle tick event for auto screen-off and animations.
 * now_ms: current time in milliseconds
 * Returns: true if needs re-render
 */
bool page_controller_tick(page_controller_t *pc, uint64_t now_ms);

/*
 * Render current page.
 * u8g2: display context
 * status: system status data
 * now_ms: current time for animations
 */
void page_controller_render(page_controller_t *pc, u8g2_t *u8g2,
                           const sys_status_t *status, uint64_t now_ms);

/*
 * Check if screen is on.
 */
bool page_controller_is_screen_on(const page_controller_t *pc);

/*
 * Check if animation is active.
 */
bool page_controller_is_animating(const page_controller_t *pc);

/*
 * Set idle timeout for auto screen-off (0 to disable).
 */
void page_controller_set_idle_timeout(page_controller_t *pc, uint32_t timeout_ms);

/*
 * Enable/disable auto screen-off feature.
 */
void page_controller_set_auto_screen_off(bool enabled);

/*
 * Check if auto screen-off is enabled.
 */
bool page_controller_is_auto_screen_off_enabled(void);

#endif

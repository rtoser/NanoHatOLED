/*
 * Animation utilities for NanoHat OLED UI
 */
#ifndef ANIM_H
#define ANIM_H

#include <stdint.h>

/* Animation types */
typedef enum {
    ANIM_NONE = 0,
    ANIM_SLIDE_LEFT,      /* Page slides left (next page) */
    ANIM_SLIDE_RIGHT,     /* Page slides right (prev page) */
    ANIM_TITLE_SHAKE,     /* Title shake for non-enterable page */
    ANIM_ENTER_MODE,      /* Enter mode transition */
    ANIM_EXIT_MODE,       /* Exit mode transition */
} anim_type_t;

/* Animation durations in ms */
#define ANIM_SLIDE_DURATION_MS   200
#define ANIM_SHAKE_DURATION_MS   400
#define ANIM_MODE_DURATION_MS    250
#define ANIM_BLINK_PERIOD_MS     300
#define ANIM_ERROR_DISPLAY_MS   1000

/* Animation state */
typedef struct {
    anim_type_t type;
    uint64_t start_ms;
    int from_page;    /* For slide animation */
    int to_page;      /* For slide animation */
} anim_state_t;

/*
 * Easing functions
 * t: progress 0.0 to 1.0
 * returns: eased value 0.0 to 1.0
 */
float ease_out_quad(float t);
float ease_in_out_quad(float t);

/*
 * Calculate animation progress
 * Returns: 0.0 to 1.0 (clamped)
 */
float anim_progress(uint64_t start_ms, uint64_t now_ms, uint64_t duration_ms);

/*
 * Calculate title shake offset
 * progress: 0.0 to 1.0
 * Returns: X offset in pixels (positive = right, negative = left)
 */
int anim_shake_offset(float progress);

/*
 * Check if animation is complete
 */
int anim_is_complete(const anim_state_t *state, uint64_t now_ms);

/*
 * Calculate slide X offset for page transition
 * progress: 0.0 to 1.0
 * direction: ANIM_SLIDE_LEFT or ANIM_SLIDE_RIGHT
 * is_outgoing: true for the page being replaced
 * Returns: X offset in pixels
 */
int anim_slide_offset(float progress, anim_type_t direction, int is_outgoing);

#endif

#include "anim.h"
#include "page.h"

float ease_out_quad(float t) {
    return t * (2.0f - t);
}

float ease_in_out_quad(float t) {
    if (t < 0.5f) {
        return 2.0f * t * t;
    } else {
        return 1.0f - 2.0f * (1.0f - t) * (1.0f - t);
    }
}

float anim_progress(uint64_t start_ms, uint64_t now_ms, uint64_t duration_ms) {
    if (now_ms <= start_ms) return 0.0f;
    if (duration_ms == 0) return 1.0f;

    uint64_t elapsed = now_ms - start_ms;
    if (elapsed >= duration_ms) return 1.0f;

    return (float)elapsed / (float)duration_ms;
}

int anim_shake_offset(float progress) {
    /*
     * Damped oscillation pattern:
     * 0.00 - 0.16: 0 -> +8
     * 0.16 - 0.33: +8 -> -8
     * 0.33 - 0.49: -8 -> +6
     * 0.49 - 0.65: +6 -> -6
     * 0.65 - 0.81: -6 -> +3
     * 0.81 - 1.00: +3 -> 0
     */
    if (progress <= 0.0f) return 0;
    if (progress >= 1.0f) return 0;

    /* Keyframe positions and amplitudes */
    static const float keyframes[][2] = {
        {0.00f, 0.0f},
        {0.16f, 8.0f},
        {0.33f, -8.0f},
        {0.49f, 6.0f},
        {0.65f, -6.0f},
        {0.81f, 3.0f},
        {1.00f, 0.0f},
    };
    const int num_keyframes = sizeof(keyframes) / sizeof(keyframes[0]);

    /* Find surrounding keyframes */
    for (int i = 0; i < num_keyframes - 1; i++) {
        float t0 = keyframes[i][0];
        float t1 = keyframes[i + 1][0];
        if (progress >= t0 && progress < t1) {
            float v0 = keyframes[i][1];
            float v1 = keyframes[i + 1][1];
            float local_t = (progress - t0) / (t1 - t0);
            return (int)(v0 + (v1 - v0) * local_t);
        }
    }

    return 0;
}

int anim_is_complete(const anim_state_t *state, uint64_t now_ms) {
    if (!state || state->type == ANIM_NONE) return 1;

    uint64_t duration;
    switch (state->type) {
        case ANIM_SLIDE_LEFT:
        case ANIM_SLIDE_RIGHT:
            duration = ANIM_SLIDE_DURATION_MS;
            break;
        case ANIM_TITLE_SHAKE:
            duration = ANIM_SHAKE_DURATION_MS;
            break;
        case ANIM_ENTER_MODE:
        case ANIM_EXIT_MODE:
            duration = ANIM_MODE_DURATION_MS;
            break;
        default:
            return 1;
    }

    return (now_ms - state->start_ms) >= duration;
}

int anim_slide_offset(float progress, anim_type_t direction, int is_outgoing) {
    float eased = ease_out_quad(progress);

    if (direction == ANIM_SLIDE_LEFT) {
        /* Next page: outgoing slides left, incoming slides in from right */
        if (is_outgoing) {
            return (int)(-SCREEN_WIDTH * eased);
        } else {
            return (int)(SCREEN_WIDTH * (1.0f - eased));
        }
    } else if (direction == ANIM_SLIDE_RIGHT) {
        /* Prev page: outgoing slides right, incoming slides in from left */
        if (is_outgoing) {
            return (int)(SCREEN_WIDTH * eased);
        } else {
            return (int)(-SCREEN_WIDTH * (1.0f - eased));
        }
    }

    return 0;
}

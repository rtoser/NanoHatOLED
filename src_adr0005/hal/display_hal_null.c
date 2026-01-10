/*
 * Null display driver for testing
 *
 * Provides stub u8g2 context for host testing.
 */
#include "display_hal.h"
#include "u8g2_stub.h"

#include <stddef.h>

static u8g2_t g_u8g2_stub;
static bool g_initialized = false;
static bool g_power_on = false;

static int null_init(void) {
    if (g_initialized) return 0;

    /* Initialize stub u8g2 */
    g_u8g2_stub.draw_color = 1;
    u8g2_SetMaxClipWindow(&g_u8g2_stub);

    g_initialized = true;
    g_power_on = true;
    return 0;
}

static void null_cleanup(void) {
    g_initialized = false;
    g_power_on = false;
}

static u8g2_t *null_get_u8g2(void) {
    if (!g_initialized) return NULL;
    return &g_u8g2_stub;
}

static void null_set_power(bool on) {
    g_power_on = on;
}

static void null_send_buffer(void) {
    /* No-op for null driver */
}

static void null_clear_buffer(void) {
    /* No-op for null driver */
}

static const display_hal_ops_t null_ops = {
    .init = null_init,
    .cleanup = null_cleanup,
    .get_u8g2 = null_get_u8g2,
    .set_power = null_set_power,
    .send_buffer = null_send_buffer,
    .clear_buffer = null_clear_buffer,
};

const display_hal_ops_t *display_hal = &null_ops;

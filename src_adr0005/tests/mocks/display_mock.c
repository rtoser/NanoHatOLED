/*
 * Display mock for testing
 */
#include "display_mock.h"

#include <stddef.h>
#include <string.h>

#include "hal/display_hal.h"
#include "hal/u8g2_stub.h"

static u8g2_t g_u8g2_mock;
static bool g_initialized = false;
static bool g_power_on = false;
static uint32_t g_send_count = 0;
static uint32_t g_clear_count = 0;

static int mock_init(void) {
    if (g_initialized) return 0;

    g_u8g2_mock.draw_color = 1;
    u8g2_SetMaxClipWindow(&g_u8g2_mock);

    g_initialized = true;
    g_power_on = true;
    g_send_count = 0;
    g_clear_count = 0;
    return 0;
}

static void mock_cleanup(void) {
    g_initialized = false;
    g_power_on = false;
}

static u8g2_t *mock_get_u8g2(void) {
    if (!g_initialized) return NULL;
    return &g_u8g2_mock;
}

static void mock_set_power(bool on) {
    g_power_on = on;
}

static void mock_send_buffer(void) {
    g_send_count++;
}

static void mock_clear_buffer(void) {
    g_clear_count++;
}

static const display_hal_ops_t g_display_mock_ops = {
    .init = mock_init,
    .cleanup = mock_cleanup,
    .get_u8g2 = mock_get_u8g2,
    .set_power = mock_set_power,
    .send_buffer = mock_send_buffer,
    .clear_buffer = mock_clear_buffer,
};

const display_hal_ops_t *display_hal = &g_display_mock_ops;

void display_mock_reset(void) {
    g_initialized = false;
    g_power_on = false;
    g_send_count = 0;
    g_clear_count = 0;
    mock_init();
}

uint32_t display_mock_send_count(void) {
    return g_send_count;
}

uint32_t display_mock_clear_count(void) {
    return g_clear_count;
}

bool display_mock_is_power_on(void) {
    return g_power_on;
}

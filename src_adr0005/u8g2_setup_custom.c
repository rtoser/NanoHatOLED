/*
 * Custom u8g2 setup - only SSD1306 128x64 I2C
 * Extracted from u8g2_d_setup.c to reduce binary size
 */
#include "u8g2.h"

/* External references */
extern const u8x8_display_info_t u8x8_ssd1306_128x64_noname_display_info;
extern uint8_t u8x8_d_ssd1306_128x64_noname(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
extern uint8_t u8x8_cad_ssd13xx_fast_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

/* Buffer for full framebuffer mode (128x64 = 1024 bytes) */
static uint8_t u8g2_buf[1024];

static uint8_t *u8g2_m_16_8_f(uint8_t *page_cnt) {
    *page_cnt = 8;
    return u8g2_buf;
}

void u8g2_Setup_ssd1306_i2c_128x64_noname_f(u8g2_t *u8g2, const u8g2_cb_t *rotation,
                                             u8x8_msg_cb byte_cb, u8x8_msg_cb gpio_and_delay_cb) {
    uint8_t tile_buf_height;
    uint8_t *buf;
    u8g2_SetupDisplay(u8g2, u8x8_d_ssd1306_128x64_noname, u8x8_cad_ssd13xx_fast_i2c,
                      byte_cb, gpio_and_delay_cb);
    buf = u8g2_m_16_8_f(&tile_buf_height);
    u8g2_SetupBuffer(u8g2, buf, tile_buf_height, u8g2_ll_hvline_vertical_top_lsb, rotation);
}

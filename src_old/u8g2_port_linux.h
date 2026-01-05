#ifndef U8G2_PORT_LINUX_H
#define U8G2_PORT_LINUX_H

#include <u8g2.h>

uint8_t u8g2_gpio_and_delay_linux(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);
uint8_t u8x8_byte_linux_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

#endif

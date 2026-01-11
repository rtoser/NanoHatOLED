/*
 * SSD1306 display driver for NanoHat OLED
 *
 * Uses u8g2 library over I2C.
 * I2C device: /dev/i2c-0 (configurable via macro)
 * I2C address: 0x3c (standard for SSD1306)
 */
#define _POSIX_C_SOURCE 200809L

#include "display_hal.h"

#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <u8g2.h>

/* I2C configuration */
#ifndef I2C_DEV_PATH
#define I2C_DEV_PATH "/dev/i2c-0"
#endif

#ifndef I2C_SLAVE_ADDR
#define I2C_SLAVE_ADDR 0x3c
#endif

/* Static state */
static u8g2_t g_u8g2;
static int g_i2c_fd = -1;
static bool g_initialized = false;

/*
 * u8g2 GPIO and delay callback for Linux.
 */
static uint8_t u8g2_gpio_and_delay_linux(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    (void)u8x8;
    (void)arg_ptr;

    switch (msg) {
        case U8X8_MSG_GPIO_AND_DELAY_INIT:
            usleep(1000);
            break;
        case U8X8_MSG_DELAY_MILLI:
            usleep(arg_int * 1000);
            break;
        case U8X8_MSG_DELAY_10MICRO:
            usleep(arg_int * 10);
            break;
        case U8X8_MSG_DELAY_100NANO:
            usleep(1);
            break;
        default:
            return 0;
    }
    return 1;
}

/*
 * u8g2 I2C byte callback for Linux.
 */
static uint8_t u8x8_byte_linux_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    (void)u8x8;

    static uint8_t buffer[256];
    static uint16_t buf_idx;
    uint8_t *data;

    switch (msg) {
        case U8X8_MSG_BYTE_SEND:
            data = (uint8_t *)arg_ptr;
            while (arg_int > 0) {
                if (buf_idx < sizeof(buffer)) {
                    buffer[buf_idx++] = *data;
                }
                data++;
                arg_int--;
            }
            break;

        case U8X8_MSG_BYTE_INIT:
            if (g_i2c_fd < 0) {
                g_i2c_fd = open(I2C_DEV_PATH, O_RDWR);
                if (g_i2c_fd < 0) {
                    perror("Failed to open I2C device");
                    return 0;
                }
                if (ioctl(g_i2c_fd, I2C_SLAVE, I2C_SLAVE_ADDR) < 0) {
                    perror("Failed to set I2C address");
                    close(g_i2c_fd);
                    g_i2c_fd = -1;
                    return 0;
                }
            }
            break;

        case U8X8_MSG_BYTE_SET_DC:
            /* I2C handles DC via control byte internally */
            break;

        case U8X8_MSG_BYTE_START_TRANSFER:
            buf_idx = 0;
            break;

        case U8X8_MSG_BYTE_END_TRANSFER:
            if (g_i2c_fd >= 0 && buf_idx > 0) {
                if (write(g_i2c_fd, buffer, buf_idx) != (ssize_t)buf_idx) {
                    perror("I2C write failed");
                    return 0;
                }
            }
            break;

        default:
            return 0;
    }
    return 1;
}

static int ssd1306_init(void) {
    if (g_initialized) {
        return 0;
    }

    /* Check I2C device accessibility */
    if (access(I2C_DEV_PATH, R_OK | W_OK) != 0) {
        perror("Cannot access I2C device");
        return -1;
    }

    /* Setup u8g2 for SSD1306 128x64 I2C */
    u8g2_Setup_ssd1306_i2c_128x64_noname_f(
        &g_u8g2,
        U8G2_R0,
        u8x8_byte_linux_i2c,
        u8g2_gpio_and_delay_linux
    );

    /* Initialize display */
    u8g2_InitDisplay(&g_u8g2);
    u8g2_SetPowerSave(&g_u8g2, 0);

    g_initialized = true;
    return 0;
}

static void ssd1306_cleanup(void) {
    if (!g_initialized) {
        return;
    }

    /* Turn off display */
    u8g2_SetPowerSave(&g_u8g2, 1);

    /* Close I2C */
    if (g_i2c_fd >= 0) {
        close(g_i2c_fd);
        g_i2c_fd = -1;
    }

    g_initialized = false;
}

static u8g2_t *ssd1306_get_u8g2(void) {
    if (!g_initialized) {
        return NULL;
    }
    return &g_u8g2;
}

static void ssd1306_set_power(bool on) {
    if (g_initialized) {
        u8g2_SetPowerSave(&g_u8g2, on ? 0 : 1);
    }
}

static void ssd1306_send_buffer(void) {
    if (g_initialized) {
        u8g2_SendBuffer(&g_u8g2);
    }
}

static void ssd1306_clear_buffer(void) {
    if (g_initialized) {
        u8g2_ClearBuffer(&g_u8g2);
    }
}

static void ssd1306_set_contrast(uint8_t level) {
    if (!g_initialized) {
        return;
    }
    /* Map 1-10 to 0-255 contrast range */
    if (level < 1) level = 1;
    if (level > 10) level = 10;
    uint8_t contrast = (uint8_t)((level - 1) * 255 / 9);
    u8g2_SetContrast(&g_u8g2, contrast);
}

static const display_hal_ops_t ssd1306_ops = {
    .init = ssd1306_init,
    .cleanup = ssd1306_cleanup,
    .get_u8g2 = ssd1306_get_u8g2,
    .set_power = ssd1306_set_power,
    .send_buffer = ssd1306_send_buffer,
    .clear_buffer = ssd1306_clear_buffer,
    .set_contrast = ssd1306_set_contrast,
};

const display_hal_ops_t *display_hal = &ssd1306_ops;

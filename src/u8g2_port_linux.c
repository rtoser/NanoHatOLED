#include <stdio.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <u8g2.h>

#define I2C_DEV "/dev/i2c-0"
#define I2C_SLAVE_ADDR 0x3c

static int i2c_fd = -1;

uint8_t u8g2_gpio_and_delay_linux(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    switch(msg) {
        case U8X8_MSG_GPIO_AND_DELAY_INIT:
            // I2C init is handled in main or specific init function
            // Here we might just introduce a small delay to settle
            usleep(1000);
            break;
        case U8X8_MSG_DELAY_MILLI:
            usleep(arg_int * 1000);
            break;
        case U8X8_MSG_DELAY_10MICRO:
            usleep(arg_int * 10);
            break;
        case U8X8_MSG_DELAY_100NANO:
            usleep(1); // Approximate
            break;
        // Since we are using HW I2C, we don't need to emulate SDA/SCL toggling
        // GPIO pins for Reset/CS can be handled here if needed, but standard SSD1306 via I2C usually doesn't need them
        default:
            return 0;
    }
    return 1;
}

uint8_t u8x8_byte_linux_i2c(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr) {
    // SSD1306 can receive up to 128 bytes per page + control byte
    // u8g2 typically sends in chunks, but we need enough buffer
    static uint8_t buffer[256];
    static uint16_t buf_idx;
    uint8_t *data;

    switch(msg) {
        case U8X8_MSG_BYTE_SEND:
            data = (uint8_t *)arg_ptr;
            while(arg_int > 0) {
                if (buf_idx < sizeof(buffer)) {
                    buffer[buf_idx++] = *data;
                }
                data++;
                arg_int--;
            }
            break;
        case U8X8_MSG_BYTE_INIT:
            if (i2c_fd < 0) {
                i2c_fd = open(I2C_DEV, O_RDWR);
                if (i2c_fd < 0) {
                    perror("Failed to open I2C device");
                    return 0;
                }
                if (ioctl(i2c_fd, I2C_SLAVE, I2C_SLAVE_ADDR) < 0) {
                    perror("Failed to set I2C address");
                    close(i2c_fd);
                    i2c_fd = -1;
                    return 0;
                }
            }
            break;
        case U8X8_MSG_BYTE_SET_DC:
            // For I2C, DC is usually handled by the control byte (0x00 for cmd, 0x40 for data)
            // But u8g2 handles this internally by calling START_TRANSFER with different types?
            // Actually u8g2 sends the control byte itself.
            break;
        case U8X8_MSG_BYTE_START_TRANSFER:
            buf_idx = 0;
            break;
        case U8X8_MSG_BYTE_END_TRANSFER:
            if (write(i2c_fd, buffer, buf_idx) != buf_idx) {
                perror("I2C write failed");
                return 0;
            }
            break;
        default:
            return 0;
    }
    return 1;
}

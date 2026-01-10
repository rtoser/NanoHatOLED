/*
 * Display HAL for NanoHat OLED
 *
 * Abstracts display hardware. Supports different controllers:
 * - SSD1306 (128x64 I2C OLED)
 * - SSD1302 (future)
 * - null (testing)
 *
 * Upper layers use u8g2_t* for rendering.
 */
#ifndef DISPLAY_HAL_H
#define DISPLAY_HAL_H

#include <stdbool.h>
#include <stdint.h>

/* Forward declaration - actual type from u8g2 or stub */
struct u8g2_struct;
typedef struct u8g2_struct u8g2_t;

/*
 * Display HAL operations.
 *
 * Implementations provide driver-specific init/cleanup.
 * Upper layers get u8g2_t* for all rendering operations.
 */
typedef struct {
    /*
     * Initialize display hardware.
     * Returns: 0 on success, -1 on failure
     */
    int (*init)(void);

    /*
     * Cleanup display hardware.
     */
    void (*cleanup)(void);

    /*
     * Get u8g2 context for rendering.
     * Returns: pointer to u8g2_t, or NULL if not initialized
     */
    u8g2_t *(*get_u8g2)(void);

    /*
     * Set display power state.
     * on: true = display on, false = display off (power save)
     */
    void (*set_power)(bool on);

    /*
     * Send buffer to display.
     * Called after rendering is complete.
     */
    void (*send_buffer)(void);

    /*
     * Clear display buffer.
     * Called before rendering new frame.
     */
    void (*clear_buffer)(void);
} display_hal_ops_t;

/*
 * Global display HAL instance.
 * Set by linking appropriate implementation.
 */
extern const display_hal_ops_t *display_hal;

/*
 * Display info for compile-time configuration.
 */
#define DISPLAY_WIDTH   128
#define DISPLAY_HEIGHT  64

/* Dual-color OLED regions */
#define DISPLAY_YELLOW_START  0
#define DISPLAY_YELLOW_END    15
#define DISPLAY_BLUE_START    16
#define DISPLAY_BLUE_END      63

#endif

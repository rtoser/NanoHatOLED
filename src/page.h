/*
 * Page interface for NanoHat OLED UI
 *
 * Plugin-style page architecture - add new pages by implementing this interface.
 */
#ifndef PAGE_H
#define PAGE_H

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations */
struct sys_status;
typedef struct sys_status sys_status_t;
struct u8g2_struct;
typedef struct u8g2_struct u8g2_t;

#ifndef U8G2_H
typedef uint16_t u8g2_uint_t;
#endif

typedef enum {
    PAGE_MODE_VIEW,     /* Browse mode - K1/K3 switch pages */
    PAGE_MODE_ENTER,    /* Enter mode - page-specific interaction */
} page_mode_t;

typedef struct page {
    const char *name;           /* Page identifier */
    bool can_enter;             /* Supports enter mode via K2 long press */

    /* Lifecycle */
    void (*init)(void);
    void (*destroy)(void);

    /* Rendering - return title string, NULL uses default */
    const char *(*get_title)(const sys_status_t *status);
    void (*render)(u8g2_t *u8g2, const sys_status_t *status,
                   page_mode_t mode, uint64_t now_ms, int x_offset);

    /* Key events - return true if handled */
    bool (*on_key)(uint8_t key, bool long_press, page_mode_t mode);

    /* Mode switch callbacks */
    void (*on_enter)(void);
    void (*on_exit)(void);

    /* Optional: Selection info for enter mode indicator (e.g., "2/5") */
    int (*get_selected_index)(void);  /* Returns 0-based index, or -1 if N/A */
    int (*get_item_count)(void);      /* Returns total items, or 0 if N/A */
} page_t;

/* Button key codes */
#define KEY_K1 1
#define KEY_K2 2
#define KEY_K3 3

/* Screen dimensions */
#define SCREEN_WIDTH  128
#define SCREEN_HEIGHT 64

/* Layout constants */
#define TITLE_BAR_HEIGHT 16
#define CONTENT_Y_START  16
#define CONTENT_HEIGHT   48
#define CONTENT_LINE_HEIGHT 16
#define CONTENT_MAX_LINES   3

#define MARGIN_LEFT   2
#define MARGIN_RIGHT  2

#endif

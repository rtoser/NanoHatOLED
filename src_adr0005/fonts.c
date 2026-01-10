/*
 * Font definitions for target build
 *
 * Maps abstract font names to real u8g2 fonts.
 * Include path: -Iu8g2/csrc is required.
 */
#include "fonts.h"

/* u8g2 library header (from u8g2/csrc) */
#include "u8g2.h"

/* Map to u8g2 built-in fonts */
const uint8_t *font_title = u8g2_font_8x13B_tf;    /* Bold title font */
const uint8_t *font_small = u8g2_font_5x7_tf;      /* Small font for indicators */
const uint8_t *font_content = u8g2_font_7x13_tf;   /* Content font */

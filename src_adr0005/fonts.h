/*
 * Font definitions for UI rendering
 *
 * Maps abstract font names to u8g2 fonts.
 * For host testing, u8g2_stub.c provides stub implementations.
 * For target build, fonts.c provides real u8g2 font mappings.
 */
#ifndef FONTS_H
#define FONTS_H

#include <stdint.h>

/* Font pointers - actual definitions in fonts.c (target) or u8g2_stub.c (host) */
extern const uint8_t *font_title;
extern const uint8_t *font_small;
extern const uint8_t *font_content;

#endif

#ifndef DISPLAY_MOCK_H
#define DISPLAY_MOCK_H

#include <stdint.h>

void display_mock_reset(void);
uint32_t display_mock_begin_count(void);
uint32_t display_mock_end_count(void);
uint32_t display_mock_draw_count(void);
const char *display_mock_last_text(void);

#endif

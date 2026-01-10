/*
 * Display mock for testing
 */
#ifndef DISPLAY_MOCK_H
#define DISPLAY_MOCK_H

#include <stdbool.h>
#include <stdint.h>

void display_mock_reset(void);
uint32_t display_mock_send_count(void);
uint32_t display_mock_clear_count(void);
bool display_mock_is_power_on(void);

#endif

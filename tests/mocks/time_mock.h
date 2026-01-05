#ifndef TIME_MOCK_H
#define TIME_MOCK_H

#include <stdint.h>

void time_mock_reset(void);
void time_mock_set_now_ms(uint64_t ms);
void time_mock_advance_ms(uint64_t delta);
uint64_t time_mock_now_ms(void);
uint64_t time_mock_now_ns(void);

#endif

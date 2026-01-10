#ifndef UBUS_MOCK_H
#define UBUS_MOCK_H

void ubus_hal_mock_set_delay(int delay_ms);
void ubus_hal_mock_set_fail_count(int count);
void ubus_hal_mock_reset(void);
int ubus_hal_mock_get_call_count(void);

#endif

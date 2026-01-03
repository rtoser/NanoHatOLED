#ifndef UBUS_SERVICE_H
#define UBUS_SERVICE_H

#include <stdbool.h>

int ubus_service_init(void);
void ubus_service_cleanup(void);

int ubus_service_status(const char *name, bool *installed, bool *running);
int ubus_service_running(const char *name, bool *running);
int ubus_service_action(const char *name, const char *action);

#endif

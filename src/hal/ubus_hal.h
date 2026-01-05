#ifndef UBUS_HAL_H
#define UBUS_HAL_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    UBUS_ACTION_START = 0,
    UBUS_ACTION_STOP,
    UBUS_ACTION_QUERY
} ubus_action_t;

typedef struct {
    char service_name[32];
    ubus_action_t action;
    uint32_t request_id;
    uint32_t timeout_ms;
    uint64_t enqueue_time_ms;
} ubus_task_t;

typedef struct {
    char service_name[32];
    ubus_action_t action;
    bool success;
    int error_code;
    uint32_t request_id;
} ubus_result_t;

typedef struct {
    int  (*init)(void);
    void (*cleanup)(void);
    int  (*invoke)(const ubus_task_t *task, ubus_result_t *result);
    int  (*register_object)(void);
    void (*unregister_object)(void);
} ubus_hal_ops_t;

extern const ubus_hal_ops_t *ubus_hal;

#endif

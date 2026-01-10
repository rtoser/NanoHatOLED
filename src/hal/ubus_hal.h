/*
 * ubus HAL for NanoHat OLED (ADR0006)
 *
 * Async interface for service status queries via OpenWrt ubus.
 * Uses ubus_invoke_async + ubus_add_uloop for single-threaded integration.
 */
#ifndef UBUS_HAL_H
#define UBUS_HAL_H

#include <stdbool.h>
#include <stddef.h>

/*
 * Status codes (compatible with libubus UBUS_STATUS_*)
 */
#define UBUS_HAL_STATUS_OK           0
#define UBUS_HAL_STATUS_NOT_FOUND    5   /* UBUS_STATUS_NOT_FOUND */
#define UBUS_HAL_STATUS_TIMEOUT      7   /* UBUS_STATUS_TIMEOUT */
#define UBUS_HAL_STATUS_CONN_FAILED  4   /* UBUS_STATUS_CONNECTION_FAILED */
#define UBUS_HAL_STATUS_ERROR        (-1)

/*
 * Query callback - invoked when async query completes.
 *
 * @param service   Service name that was queried
 * @param installed true if service exists in rc
 * @param running   true if service is currently running
 * @param status    UBUS_HAL_STATUS_* code
 * @param priv      User-provided context
 *
 * IMPORTANT: Callback constraints:
 *   - Do NOT call query_service_async() from within the callback.
 *     Connection errors may trigger abort_all_pending() which iterates
 *     pending requests; issuing new queries during iteration causes
 *     undefined behavior.
 *   - Keep callback execution short; heavy processing should be deferred.
 */
typedef void (*ubus_query_cb)(const char *service, bool installed,
                               bool running, int status, void *priv);

/*
 * Control callback - invoked when async control operation completes.
 *
 * @param service   Service name that was controlled
 * @param success   true if operation succeeded
 * @param status    UBUS_HAL_STATUS_* code
 * @param priv      User-provided context
 */
typedef void (*ubus_control_cb)(const char *service, bool success,
                                 int status, void *priv);

/*
 * ubus HAL operations
 */
typedef struct {
    /*
     * Initialize ubus connection and register with uloop.
     * Must be called after uloop_init().
     * Returns 0 on success, -1 on failure.
     */
    int (*init)(void);

    /*
     * Cleanup ubus connection.
     * Safe to call multiple times.
     */
    void (*cleanup)(void);

    /*
     * Query a single service status asynchronously.
     *
     * @param name  Service name (e.g., "dropbear")
     * @param cb    Callback invoked on completion/timeout
     * @param priv  User context passed to callback
     * @return 0 on success (callback will be invoked), -1 on invalid args
     *
     * Return value semantics:
     *   - Returns 0: Callback WILL be invoked exactly once (async or sync)
     *     This includes connection failures - callback receives CONN_FAILED.
     *   - Returns -1: Invalid arguments (null name/cb), callback NOT invoked.
     *
     * The callback is guaranteed to be called at most once per request.
     */
    int (*query_service_async)(const char *name, ubus_query_cb cb, void *priv);

    /*
     * Query multiple services in a single request (optional optimization).
     * If NULL, caller should use query_service_async() in a loop.
     *
     * @param names     Array of service names
     * @param count     Number of services
     * @param cb        Callback invoked per service
     * @param priv      User context
     * @return 0 on success, -1 on invalid args
     *
     * Same return/callback semantics as query_service_async().
     */
    int (*query_services_async)(const char **names, size_t count,
                                 ubus_query_cb cb, void *priv);

    /*
     * Start or stop a service asynchronously.
     *
     * @param name      Service name (e.g., "dropbear")
     * @param start     true to start, false to stop
     * @param cb        Callback invoked on completion
     * @param priv      User context
     * @return 0 on success (callback will be invoked), -1 on invalid args
     */
    int (*control_service_async)(const char *name, bool start,
                                  ubus_control_cb cb, void *priv);
} ubus_hal_ops_t;

/*
 * Global HAL instance - set at compile time based on BUILD_MODE
 */
extern const ubus_hal_ops_t *ubus_hal;

#endif /* UBUS_HAL_H */

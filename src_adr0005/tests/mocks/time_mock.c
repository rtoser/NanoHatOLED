#include "time_mock.h"

#include <pthread.h>

static uint64_t g_now_ms = 0;
static pthread_mutex_t g_lock = PTHREAD_MUTEX_INITIALIZER;

void time_mock_reset(void) {
    pthread_mutex_lock(&g_lock);
    g_now_ms = 0;
    pthread_mutex_unlock(&g_lock);
}

void time_mock_set_now_ms(uint64_t ms) {
    pthread_mutex_lock(&g_lock);
    g_now_ms = ms;
    pthread_mutex_unlock(&g_lock);
}

void time_mock_advance_ms(uint64_t delta) {
    pthread_mutex_lock(&g_lock);
    g_now_ms += delta;
    pthread_mutex_unlock(&g_lock);
}

uint64_t time_mock_now_ms(void) {
    pthread_mutex_lock(&g_lock);
    uint64_t now = g_now_ms;
    pthread_mutex_unlock(&g_lock);
    return now;
}

uint64_t time_mock_now_ns(void) {
    return time_mock_now_ms() * 1000000ULL;
}

/* HAL shim for host tests */
uint64_t time_hal_now_ms(void) {
    return time_mock_now_ms();
}

uint64_t time_hal_now_ns(void) {
    return time_mock_now_ns();
}

/*
 * FRANK 386 – Raspberry Pi 4 port
 * Pico SDK time API on top of the port HAL (port_hal.h).
 */
#ifndef FRANK_PORT_PICO_TIME_H
#define FRANK_PORT_PICO_TIME_H

#include "../pico.h"
#include "port_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t absolute_time_t;

static inline uint64_t time_us_64(void)         { return port_time_us(); }
static inline uint32_t time_us_32(void)         { return (uint32_t)port_time_us(); }
static inline absolute_time_t get_absolute_time(void) { return port_time_us(); }
static inline uint64_t to_us_since_boot(absolute_time_t t) { return t; }
static inline uint32_t to_ms_since_boot(absolute_time_t t) { return (uint32_t)(t / 1000); }
static inline absolute_time_t make_timeout_time_ms(uint32_t ms) { return port_time_us() + (uint64_t)ms * 1000u; }
static inline absolute_time_t make_timeout_time_us(uint64_t us) { return port_time_us() + us; }
static inline bool time_reached(absolute_time_t t) { return port_time_us() >= t; }
static inline int64_t absolute_time_diff_us(absolute_time_t from, absolute_time_t to) { return (int64_t)(to - from); }
static inline absolute_time_t delayed_by_us(absolute_time_t t, uint64_t us) { return t + us; }
static inline absolute_time_t delayed_by_ms(absolute_time_t t, uint32_t ms) { return t + (uint64_t)ms * 1000u; }

static inline void sleep_us(uint64_t us)        { port_sleep_us(us); }
static inline void sleep_ms(uint32_t ms)        { port_sleep_us((uint64_t)ms * 1000u); }
static inline void busy_wait_us(uint64_t us)    { uint64_t e = port_time_us() + us; while (port_time_us() < e) {} }
static inline void busy_wait_us_32(uint32_t us) { busy_wait_us(us); }
static inline void busy_wait_ms(uint32_t ms)    { busy_wait_us((uint64_t)ms * 1000u); }

/*
 * Repeating timers.  On the RP2350 these are hardware alarm IRQs.
 * In the port they are serviced cooperatively by port_timers_service(),
 * which the platform backend calls from its audio/service thread.
 * Only the subset the emulator uses is implemented.
 */
typedef struct repeating_timer repeating_timer_t;
typedef bool (*repeating_timer_callback_t)(repeating_timer_t *rt);

struct repeating_timer {
    int64_t delay_us;
    uint64_t next_us;
    repeating_timer_callback_t callback;
    void *user_data;
    struct repeating_timer *next;
    bool active;
};

bool add_repeating_timer_us(int64_t delay_us, repeating_timer_callback_t cb,
                            void *user_data, repeating_timer_t *out);
static inline bool add_repeating_timer_ms(int32_t delay_ms, repeating_timer_callback_t cb,
                                          void *user_data, repeating_timer_t *out)
{
    return add_repeating_timer_us((int64_t)delay_ms * 1000, cb, user_data, out);
}
bool cancel_repeating_timer(repeating_timer_t *timer);

/* Run every timer that is due at time `now_us`. */
void port_timers_service(uint64_t now_us);

#ifdef __cplusplus
}
#endif

#endif

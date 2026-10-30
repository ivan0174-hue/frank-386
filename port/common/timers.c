/*
 * FRANK 386 – Raspberry Pi 4 port
 * Cooperative implementation of the Pico SDK repeating-timer API.
 *
 * Only the emulated AdLib uses a repeating timer (1 ms OPL batch
 * renderer).  The RP2350 serviced it from a hardware alarm IRQ; here the
 * audio producer calls port_timers_service() with the *audio* clock, so
 * the OPL renderer stays locked to the sample stream regardless of how
 * the backend schedules its audio thread.
 */
#include <stddef.h>
#include "pico/time.h"

static repeating_timer_t *timer_list;

bool add_repeating_timer_us(int64_t delay_us, repeating_timer_callback_t cb,
                            void *user_data, repeating_timer_t *out)
{
    if (!out || !cb || delay_us == 0)
        return false;
    out->delay_us  = delay_us < 0 ? -delay_us : delay_us;
    out->callback  = cb;
    out->user_data = user_data;
    out->next_us   = 0;          /* scheduled on first service call */
    out->active    = true;
    out->next      = timer_list;
    __sync_synchronize();
    timer_list = out;
    return true;
}

bool cancel_repeating_timer(repeating_timer_t *timer)
{
    if (!timer || !timer->active)
        return false;
    timer->active = false;
    return true;
}

void port_timers_service(uint64_t now_us)
{
    for (repeating_timer_t *t = timer_list; t; t = t->next) {
        if (!t->active)
            continue;
        if (t->next_us == 0)
            t->next_us = now_us + (uint64_t)t->delay_us;
        /* Catch up at most a few periods so a stall cannot spin forever. */
        int guard = 4;
        while (t->active && now_us >= t->next_us && guard--) {
            t->next_us += (uint64_t)t->delay_us;
            if (!t->callback(t))
                t->active = false;
        }
        if (now_us >= t->next_us)
            t->next_us = now_us + (uint64_t)t->delay_us;
    }
}

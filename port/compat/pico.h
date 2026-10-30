/*
 * FRANK 386 – Raspberry Pi 4 port
 * Minimal replacement for the Pico SDK <pico.h>.
 *
 * The emulator core was written for the RP2350 and decorates hot
 * functions/data with linker-section macros (__not_in_flash_func etc.).
 * On the Pi 4 everything already lives in SDRAM behind a cache, so the
 * macros simply vanish.
 */
#ifndef FRANK_PORT_PICO_H
#define FRANK_PORT_PICO_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef unsigned int uint;

#ifndef __not_in_flash_func
#define __not_in_flash_func(f)              f
#endif
#ifndef __not_in_flash
#define __not_in_flash(group)
#endif
#ifndef __time_critical_func
#define __time_critical_func(f)             f
#endif
#ifndef __no_inline_not_in_flash_func
#define __no_inline_not_in_flash_func(f)    __attribute__((noinline)) f
#endif
#ifndef __in_flash
#define __in_flash(group)
#endif
#ifndef __scratch_x
#define __scratch_x(group)
#endif
#ifndef __scratch_y
#define __scratch_y(group)
#endif
#ifndef __uninitialized_ram
#define __uninitialized_ram(v)              v
#endif
#ifndef __isr
#define __isr
#endif
#ifndef __force_inline
#define __force_inline                      inline __attribute__((always_inline))
#endif
#ifndef __noinline
#define __noinline                          __attribute__((noinline))
#endif
#ifndef __aligned
#define __aligned(x)                        __attribute__((aligned(x)))
#endif
#ifndef __packed
#define __packed                            __attribute__((packed))
#endif
#ifndef __unused
#define __unused                            __attribute__((unused))
#endif
#ifndef __used
#define __used                              __attribute__((used))
#endif
#ifndef __unreachable
#define __unreachable()                     __builtin_unreachable()
#endif
#ifndef __dmb
#define __dmb()                             __sync_synchronize()
#endif
#ifndef __dsb
#define __dsb()                             __sync_synchronize()
#endif
#ifndef __isb
#define __isb()                             __sync_synchronize()
#endif
#ifndef __compiler_memory_barrier
#define __compiler_memory_barrier()         __asm__ volatile("" ::: "memory")
#endif
#ifndef __sev
#define __sev()                             ((void)0)
#endif
#ifndef __wfe
#define __wfe()                             ((void)0)
#endif
#ifndef __wfi
#define __wfi()                             ((void)0)
#endif
#ifndef tight_loop_contents
#define tight_loop_contents()               ((void)0)
#endif

#ifndef __fast_mul
#define __fast_mul(a, b)                    ((a) * (b))
#endif
#if !defined(__cplusplus) && !defined(static_assert)
#define static_assert _Static_assert
#endif
#ifndef count_of
#define count_of(a)                         (sizeof(a) / sizeof((a)[0]))
#endif

#ifdef __cplusplus
}
#endif

#endif /* FRANK_PORT_PICO_H */

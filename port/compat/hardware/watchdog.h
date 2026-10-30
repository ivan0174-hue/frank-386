#ifndef FRANK_PORT_HW_WATCHDOG_H
#define FRANK_PORT_HW_WATCHDOG_H
#include "../pico.h"
#include "port_hal.h"
static inline void watchdog_reboot(uint32_t pc, uint32_t sp, uint32_t delay_ms)
{ (void)pc; (void)sp; (void)delay_ms; port_reboot(); }
#endif

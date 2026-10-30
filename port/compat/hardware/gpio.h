#ifndef FRANK_PORT_HW_GPIO_H
#define FRANK_PORT_HW_GPIO_H
/* The Pi 4 port has no direct GPIO peripherals in the emulator core. */
#include "../pico.h"
#define GPIO_OUT 1
#define GPIO_IN  0
static inline void gpio_init(uint pin) { (void)pin; }
static inline void gpio_set_dir(uint pin, bool out) { (void)pin; (void)out; }
static inline void gpio_put(uint pin, bool v) { (void)pin; (void)v; }
static inline bool gpio_get(uint pin) { (void)pin; return false; }
#endif

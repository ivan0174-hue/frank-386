#ifndef FRANK_PORT_MAIN_H
#define FRANK_PORT_MAIN_H
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Mount the SD card, load config.ini, create the PC and load the BIOS. */
bool frank_init(void);
/* Run the emulator forever (never returns). */
void frank_run(void);
#ifdef __cplusplus
}
#endif
#endif

#ifndef FRANK_PORT_HID_KEYMAP_H
#define FRANK_PORT_HID_KEYMAP_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
/* Returns a Linux KEY_* code, or 0 if the usage is not mapped. */
int hid_to_linux_keycode(uint8_t hid_code);
#ifdef __cplusplus
}
#endif
#endif

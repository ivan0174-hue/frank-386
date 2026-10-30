/*
 * FRANK 386 – Raspberry Pi 4 port
 *
 * Hardware abstraction layer between the platform-neutral emulator
 * front-end (port/common) and a concrete backend:
 *   - port/circle : bare-metal Raspberry Pi 4 (Circle + newlib)
 *   - port/sdl    : Linux/SDL2 (Raspberry Pi OS or a desktop, for testing)
 *
 * All functions are implemented by the backend.
 */
#ifndef FRANK_PORT_HAL_H
#define FRANK_PORT_HAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- time ------------------------------------------------------------ */
uint64_t port_time_us(void);          /* monotonic microseconds since boot */
void     port_sleep_us(uint64_t us);  /* may yield */
void     port_reboot(void);           /* full system restart */

/* ---- storage (raw SD card / image, 512-byte sectors) ------------------ */
bool     port_disk_init(void);
bool     port_disk_read(uint32_t sector, uint32_t count, void *buf);
bool     port_disk_write(uint32_t sector, uint32_t count, const void *buf);
uint32_t port_disk_sector_count(void);
void     port_disk_sync(void);

/* ---- video ----------------------------------------------------------- */
/*
 * Present the emulator frame.  `fb` is XRGB8888, `stride` in pixels.
 * (sx, sy, sw, sh) is the active guest picture inside the frame; the
 * backend scales that rectangle to the physical display.
 */
void     port_video_present(const uint32_t *fb, int stride,
                            int sx, int sy, int sw, int sh);

/* ---- input (Linux KEY_* codes, see src/hid.h / ps2kbd) ---------------- */
bool     port_poll_key(int *is_down, int *keycode);
bool     port_poll_mouse(int16_t *dx, int16_t *dy, int8_t *dz, uint8_t *buttons);
bool     port_poll_joystick(int *x, int *y, uint8_t *buttons);

/* ---- misc ------------------------------------------------------------ */
const char *port_platform_name(void);

#ifdef __cplusplus
}
#endif

#endif

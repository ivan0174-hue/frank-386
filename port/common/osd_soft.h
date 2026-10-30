#ifndef FRANK_PORT_OSD_SOFT_H
#define FRANK_PORT_OSD_SOFT_H
#include <stdint.h>
#include <stdbool.h>
#ifdef __cplusplus
extern "C" {
#endif
#define OSD_PIXEL_W (80 * 8)
#define OSD_PIXEL_H (25 * 16)
/* Draw the 640x400 OSD into an XRGB8888 frame at (x0, y0). */
void osd_render_xrgb(uint32_t *fb, int stride, int x0, int y0);
uint32_t osd_get_generation(void);
#ifdef __cplusplus
}
#endif
#endif

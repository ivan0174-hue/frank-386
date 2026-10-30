/*
 * FRANK 386 – Raspberry Pi 4 port
 * Software OSD (disk manager, settings menu, welcome/error screens).
 *
 * The buffer API and plasma effect are taken unchanged from
 * drivers/vga/vga_osd.c; only the output stage differs: instead of being
 * mixed into the RP2350 scanline ISR, the 80x25 cell buffer is rendered
 * into the XRGB8888 frame that is handed to the display backend.
 */
#include <string.h>
#include <stdio.h>
#include "vga_osd.h"
#include "font8x16.h"
#include "osd_soft.h"

static uint8_t osd_buffer[OSD_COLS * OSD_ROWS * 2];
static volatile bool osd_visible = false;
static volatile uint32_t osd_generation = 0;

#define BOX_TL  0xDA
#define BOX_TR  0xBF
#define BOX_BL  0xC0
#define BOX_BR  0xD9
#define BOX_H   0xC4
#define BOX_V   0xB3

/* Standard CGA/VGA 16-colour text palette, XRGB8888. */
static const uint32_t osd_palette[16] = {
    0x000000, 0x0000AA, 0x00AA00, 0x00AAAA, 0xAA0000, 0xAA00AA, 0xAA5500, 0xAAAAAA,
    0x555555, 0x5555FF, 0x55FF55, 0x55FFFF, 0xFF5555, 0xFF55FF, 0xFFFF55, 0xFFFFFF,
};

void osd_init(void) {
    osd_clear();
    osd_visible = false;
}

void osd_show(void) {
    osd_visible = true;
    osd_generation++;
}

void osd_hide(void) {
    osd_visible = false;
    osd_generation++;
}

bool osd_is_visible(void) {
    return osd_visible;
}

uint32_t osd_get_generation(void) {
    return osd_generation;
}

void osd_clear(void) {
    // Fill with spaces and default attribute
    for (int i = 0; i < OSD_COLS * OSD_ROWS; i++) {
        osd_buffer[i * 2] = ' ';
        osd_buffer[i * 2 + 1] = OSD_ATTR_NORMAL;
    }
}

void osd_putchar(int x, int y, char ch, uint8_t attr) {
    if (x < 0 || x >= OSD_COLS || y < 0 || y >= OSD_ROWS) return;

    int idx = (y * OSD_COLS + x) * 2;
    osd_buffer[idx] = (uint8_t)ch;
    osd_buffer[idx + 1] = attr;
}

void osd_print(int x, int y, const char *str, uint8_t attr) {
    while (*str && x < OSD_COLS) {
        osd_putchar(x++, y, *str++, attr);
    }
}

void osd_print_center(int y, const char *str, uint8_t attr) {
    int len = strlen(str);
    int x = (OSD_COLS - len) / 2;
    if (x < 0) x = 0;
    osd_print(x, y, str, attr);
}

void osd_draw_box(int x, int y, int w, int h, uint8_t attr) {
    if (w < 2 || h < 2) return;

    // Corners
    osd_putchar(x, y, BOX_TL, attr);
    osd_putchar(x + w - 1, y, BOX_TR, attr);
    osd_putchar(x, y + h - 1, BOX_BL, attr);
    osd_putchar(x + w - 1, y + h - 1, BOX_BR, attr);

    // Top and bottom edges
    for (int i = 1; i < w - 1; i++) {
        osd_putchar(x + i, y, BOX_H, attr);
        osd_putchar(x + i, y + h - 1, BOX_H, attr);
    }

    // Left and right edges
    for (int i = 1; i < h - 1; i++) {
        osd_putchar(x, y + i, BOX_V, attr);
        osd_putchar(x + w - 1, y + i, BOX_V, attr);
    }
}

void osd_draw_box_titled(int x, int y, int w, int h, const char *title, uint8_t attr) {
    osd_draw_box(x, y, w, h, attr);

    // Draw title centered in top border
    int title_len = strlen(title);
    int title_x = x + (w - title_len - 4) / 2;
    if (title_x < x + 1) title_x = x + 1;

    osd_putchar(title_x, y, ' ', attr);
    osd_print(title_x + 1, y, title, attr);
    osd_putchar(title_x + 1 + title_len, y, ' ', attr);
}

void osd_fill(int x, int y, int w, int h, char ch, uint8_t attr) {
    for (int row = y; row < y + h && row < OSD_ROWS; row++) {
        for (int col = x; col < x + w && col < OSD_COLS; col++) {
            osd_putchar(col, row, ch, attr);
        }
    }
}

uint8_t *osd_get_buffer(void) {
    return osd_buffer;
}

// Fast integer sine approximation for plasma effect (returns -127 to 127)
static int8_t fast_sin(int angle) {
    static const int8_t sin_table[65] = {
        0, 3, 6, 9, 12, 16, 19, 22, 25, 28, 31, 34, 37, 40, 43, 46,
        49, 51, 54, 57, 60, 62, 65, 67, 70, 72, 75, 77, 79, 81, 83, 85,
        87, 89, 91, 93, 94, 96, 97, 99, 100, 101, 103, 104, 105, 106, 107, 108,
        108, 109, 110, 110, 111, 111, 112, 112, 112, 112, 113, 113, 113, 113, 113, 113, 113
    };
    angle = angle & 255;
    int quadrant = angle >> 6;
    int idx = angle & 63;

    switch (quadrant) {
        case 0: return sin_table[idx];
        case 1: return sin_table[64 - idx];
        case 2: return -sin_table[idx];
        default: return -sin_table[64 - idx];
    }
}

void osd_draw_plasma_background(int seed, int wx, int wy, int ww, int wh) {
    static const char shade_chars[] = {' ', '\xB0', '\xB1', '\xB2', '\xDB'};

    for (int y = 0; y < OSD_ROWS; y++) {
        for (int x = 0; x < OSD_COLS; x++) {
            // Skip the window area
            if (x >= wx && x < wx + ww && y >= wy && y < wy + wh) {
                continue;
            }

            // Plasma formula with multiple sine waves
            int v1 = fast_sin(x * 8 + seed);
            int v2 = fast_sin(y * 14 + seed * 2);
            int v3 = fast_sin((x + y) * 5 + seed);
            int v4 = fast_sin((x - y) * 7 - seed * 2);

            // Normalize to 0-4 range for shade index
            int value = (v1 + v2 + v3 + v4 + 512) >> 8;
            if (value > 4) value = 4;
            if (value < 0) value = 0;

            char ch = shade_chars[value];
            osd_putchar(x, y, ch, OSD_ATTR(OSD_DARKGRAY, OSD_BLACK));
        }
    }
}


void osd_render_xrgb(uint32_t *fb, int stride, int x0, int y0)
{
    for (int row = 0; row < OSD_ROWS; row++) {
        for (int col = 0; col < OSD_COLS; col++) {
            const uint8_t ch   = osd_buffer[(row * OSD_COLS + col) * 2];
            const uint8_t attr = osd_buffer[(row * OSD_COLS + col) * 2 + 1];
            const uint32_t fg = osd_palette[attr & 0x0F];
            const uint32_t bg = osd_palette[(attr >> 4) & 0x0F];
            const uint8_t *glyph = &font_8x16[ch * 16];
            uint32_t *dst = fb + (size_t)(y0 + row * 16) * stride + x0 + col * 8;
            for (int gy = 0; gy < 16; gy++) {
                const uint8_t bits = glyph[gy];
                /* font_8x16 stores the leftmost pixel in bit 0 */
                for (int gx = 0; gx < 8; gx++)
                    dst[gx] = (bits >> gx) & 1 ? fg : bg;
                dst += stride;
            }
        }
    }
}

/* Stubs kept for API compatibility with the RP2350 scanline renderer. */
void osd_render_line_vga(uint32_t line, uint32_t *output_buffer) { (void)line; (void)output_buffer; }
void osd_render_line_hdmi(uint32_t line, uint8_t *output_buffer) { (void)line; (void)output_buffer; }

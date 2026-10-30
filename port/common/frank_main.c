/*
 * FRANK 386 – Raspberry Pi 4 port
 *
 * Platform-neutral emulator front-end.  This is src/main.c from the
 * RP2350 firmware with every piece of RP2350 hardware (PIO video, PSRAM,
 * clocks, PS/2 GPIO, I2S) replaced by calls into port_hal.h:
 *
 *   - guest RAM / VRAM come from the heap (the Pi 4 has gigabytes of it)
 *   - video uses the software VGA renderer in src/vga.c (the generic
 *     Tiny386 path), then hands the finished XRGB frame to the backend
 *   - input arrives as Linux KEY_* codes, exactly what the RP2350 USB
 *     and PS/2 drivers produced, so the hotkeys, disk manager and
 *     settings menu work unchanged
 *
 * Copyright (c) 2026 Mikhail Matveev <xtreme@rh1.tech> (original main.c)
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pico/stdlib.h"
#include "port_hal.h"
#include "frank_main.h"
#include "osd_soft.h"

#include "board_config.h"
#include "ff.h"
#include "audio.h"
#include "pc.h"
#include "ini.h"
#include "debug.h"
#include "diskui.h"
#include "settingsui.h"
#include "config_save.h"
#include "vga_osd.h"
#include "autotype.h"
#include "gameport.h"

#ifndef FRANK_386_VERSION_MAJOR
#define FRANK_386_VERSION_MAJOR 1
#endif
#ifndef FRANK_386_VERSION_MINOR
#define FRANK_386_VERSION_MINOR 0
#endif

/* Linux input keycodes used by the hotkeys (same values as src/hid.h users). */
#define KEY_F11       87
#define KEY_F12       88
#define KEY_LEFTMETA  125
#define KEY_RIGHTMETA 126

/* Emulator frame buffer: large enough for VBE 1024x768. */
#define FB_W 1024
#define FB_H 768

/* Default guest configuration for the Pi 4 (overridable in config.ini). */
#ifndef FRANK_DEFAULT_MEM_MB
#define FRANK_DEFAULT_MEM_MB 16
#endif
#ifndef FRANK_DEFAULT_VGA_MEM_KB
#define FRANK_DEFAULT_VGA_MEM_KB 4096
#endif

PC *pc = NULL;
/* Referenced by the settings menu; the Pi 4 always drives HDMI. */
bool SELECT_VGA = false;

static PCConfig config;
static FATFS fatfs;
static uint32_t *framebuffer;     /* guest picture, XRGB8888 */
static uint32_t *osd_frame;       /* 640x400 OSD frame */
static volatile int frame_dirty = 1;
static bool video_ready = false;

/*===========================================================================
 * Platform HAL used by the emulator core
 *=========================================================================*/

uint32_t get_uticks(void)
{
    return (uint32_t)port_time_us();
}

void *pcmalloc(long size)
{
    /* 64-byte alignment keeps guest RAM cache-line aligned on the A72. */
    void *p = NULL;
    size_t sz = ((size_t)size + 63u) & ~(size_t)63u;
    p = aligned_alloc(64, sz);
    return p;
}

int load_rom(void *phys_mem, const char *file, uword addr, int backward)
{
    FIL fp;
    UINT bytes_read;
    char path[256];

    snprintf(path, sizeof(path), "386/%s", file);
    if (f_open(&fp, path, FA_READ) != FR_OK) {
        printf("Failed to open ROM: %s\n", path);
        return -1;
    }
    FSIZE_t size = f_size(&fp);
    uint8_t *dest = (uint8_t *)phys_mem + (backward ? addr - size : addr);
    FRESULT res = f_read(&fp, dest, size, &bytes_read);
    f_close(&fp);
    if (res != FR_OK || bytes_read != size) {
        printf("ERROR: failed to read ROM %s (%d)\n", path, res);
        return -1;
    }
    printf("Loaded %s (%lu bytes) at 0x%05lx\n", path, (unsigned long)size,
           (unsigned long)(backward ? addr - size : addr));
    return (int)size;
}

/*===========================================================================
 * Video
 *=========================================================================*/

static void vga_redraw(void *opaque, int x, int y, int w, int h)
{
    (void)opaque; (void)x; (void)y; (void)w; (void)h;
    frame_dirty = 1;
}

/*
 * Size of the active guest picture, mirroring the geometry used by
 * vga_text_refresh()/vga_graphic_refresh() in src/vga.c (both centre the
 * picture inside the frame buffer).
 */
static void guest_picture_rect(int *x, int *y, int *w, int *h)
{
    VGAState *s = pc ? pc->vga : NULL;
    int pw = 640, ph = 400;

    if (s && s->graphic_mode == 1) {
        int cheight = (s->cr[9] & 0x1f) + 1;
        int cwidth = (!s->force_8dm && !(s->sr[1] & 0x01)) ? 9 : 8;
        int cols = s->cr[0x01] + 1;
        int lines = s->cr[0x12] | ((s->cr[0x07] & 0x02) << 7) | ((s->cr[0x07] & 0x40) << 3);
        lines = (lines + 1) / cheight;
        pw = cols * cwidth;
        ph = lines * cheight;
    } else if (s && s->graphic_mode == 2) {
        int shift_control = (s->gr[0x05] >> 5) & 3;
        pw = (s->cr[0x01] + 1) * 8;
        ph = (s->cr[0x12] | ((s->cr[0x07] & 0x02) << 7) | ((s->cr[0x07] & 0x40) << 3)) + 1;
        if (shift_control == 1 && (s->sr[0x01] & 8))
            pw *= 2;
    }
    if (pw < 64 || pw > FB_W) pw = pw > FB_W ? FB_W : 640;
    if (ph < 64 || ph > FB_H) ph = ph > FB_H ? FB_H : 400;
    *w = pw;
    *h = ph;
    *x = (FB_W - pw) / 2;
    *y = (FB_H - ph) / 2;
}

/* Hand the current picture (guest or OSD) to the display backend. */
static void present_frame(void)
{
    if (!video_ready)
        return;
    if (osd_is_visible()) {
        osd_render_xrgb(osd_frame, OSD_PIXEL_W, 0, 0);
        port_video_present(osd_frame, OSD_PIXEL_W, 0, 0, OSD_PIXEL_W, OSD_PIXEL_H);
        return;
    }
    int x, y, w, h;
    guest_picture_rect(&x, &y, &w, &h);
    port_video_present(framebuffer, FB_W, x, y, w, h);
    frame_dirty = 0;
}

static void video_init(void)
{
    framebuffer = pcmalloc((long)FB_W * FB_H * 4);
    osd_frame = pcmalloc((long)OSD_PIXEL_W * OSD_PIXEL_H * 4);
    if (!framebuffer || !osd_frame) {
        printf("FATAL: cannot allocate frame buffers\n");
        for (;;) sleep_ms(1000);
    }
    memset(framebuffer, 0, (size_t)FB_W * FB_H * 4);
    memset(osd_frame, 0, (size_t)OSD_PIXEL_W * OSD_PIXEL_H * 4);
    osd_init();
    video_ready = true;
}

/*===========================================================================
 * Error screens (same look as the RP2350 firmware)
 *=========================================================================*/

static void show_error_screen(const char *title, const char *message, const char *detail)
{
    printf("FATAL ERROR: %s\n  %s\n", title, message);
    if (detail) printf("  %s\n", detail);
    if (!video_ready)
        for (;;) sleep_ms(1000);

    osd_init();
    osd_fill(0, 0, OSD_COLS, OSD_ROWS, ' ', OSD_ATTR(OSD_WHITE, OSD_BLACK));
    int box_w = 60, box_h = 10;
    int box_x = (OSD_COLS - box_w) / 2, box_y = (OSD_ROWS - box_h) / 2;
    uint8_t error_attr = OSD_ATTR(OSD_WHITE, OSD_RED);
    osd_fill(box_x, box_y, box_w, box_h, ' ', error_attr);
    osd_draw_box_titled(box_x, box_y, box_w, box_h, title, error_attr);
    osd_print(box_x + 3, box_y + 3, message, OSD_ATTR(OSD_YELLOW, OSD_RED));
    if (detail && detail[0])
        osd_print(box_x + 3, box_y + 5, detail, error_attr);
    osd_print(box_x + 3, box_y + box_h - 2, "Check the SD card and restart.", error_attr);
    osd_show();
    for (;;) {
        present_frame();
        sleep_ms(100);
    }
}

static void show_warning_screen(const char *title, const char *message, int delay_ms)
{
    printf("WARNING: %s - %s\n", title, message);
    if (!video_ready)
        return;
    osd_init();
    osd_fill(0, 0, OSD_COLS, OSD_ROWS, ' ', OSD_ATTR(OSD_WHITE, OSD_BLACK));
    int box_w = 60, box_h = 8;
    int box_x = (OSD_COLS - box_w) / 2, box_y = (OSD_ROWS - box_h) / 2;
    uint8_t warn_attr = OSD_ATTR(OSD_BLACK, OSD_YELLOW);
    osd_fill(box_x, box_y, box_w, box_h, ' ', warn_attr);
    osd_draw_box_titled(box_x, box_y, box_w, box_h, title, warn_attr);
    osd_print(box_x + 3, box_y + 3, message, warn_attr);
    osd_show();
    for (int t = 0; t < delay_ms; t += 50) {
        present_frame();
        sleep_ms(50);
    }
    osd_hide();
}

/*===========================================================================
 * Input
 *=========================================================================*/

static bool win_key_pressed = false;

static void set_paused(bool paused)
{
    if (!pc) return;
    pc->paused = paused;
    audio_set_enabled(!paused);
    if (!paused) {
        /* Force a full redraw of the guest picture after a menu. */
        pc->full_update = 2;
        frame_dirty = 1;
    }
}

/* Hotkeys and menu routing. Returns true if the key belongs to the guest. */
static bool process_keycode(int is_down, int keycode)
{
    if (keycode == KEY_LEFTMETA || keycode == KEY_RIGHTMETA)
        win_key_pressed = is_down;

    if (is_down && keycode == KEY_F12 && win_key_pressed) {
        if (!diskui_is_open() && !settingsui_is_open()) {
            diskui_open();
            set_paused(true);
        } else if (diskui_is_open()) {
            diskui_close();
            set_paused(false);
        }
        return false;
    }
    if (is_down && keycode == KEY_F11 && win_key_pressed) {
        if (!settingsui_is_open() && !diskui_is_open()) {
            settingsui_open();
            set_paused(true);
        } else if (settingsui_is_open()) {
            settingsui_close();
            set_paused(false);
        }
        return false;
    }
    if (diskui_is_open()) {
        diskui_handle_key(keycode, is_down);
        if (!diskui_is_open() && pc && pc->paused)
            set_paused(false);
        return false;
    }
    if (settingsui_is_open()) {
        settingsui_handle_key(keycode, is_down);
        if (!settingsui_is_open() && pc && pc->paused)
            set_paused(false);
        return false;
    }
    return true;
}

typedef struct {
    int32_t rem_x, rem_y;
    int setting;
} MouseScaleState;

static void mouse_scale_delta(MouseScaleState *st, int16_t *dx, int16_t *dy)
{
    static const uint8_t num[9] = { 1, 1, 1, 2, 1, 3, 2, 3, 4 };
    static const uint8_t den[9] = { 4, 3, 2, 3, 1, 2, 1, 1, 1 };
    int setting = config_get_mouse_sensitivity();
    if (setting < 0 || setting > 8)
        setting = 4;
    if (st->setting != setting) {
        st->setting = setting;
        st->rem_x = st->rem_y = 0;
    }
    if (setting == 4)
        return;
    int32_t ax = st->rem_x + (int32_t)*dx * num[setting];
    int32_t ay = st->rem_y + (int32_t)*dy * num[setting];
    int32_t ox = ax / den[setting];
    int32_t oy = ay / den[setting];
    st->rem_x = ax - ox * den[setting];
    st->rem_y = ay - oy * den[setting];
    *dx = (int16_t)ox;
    *dy = (int16_t)oy;
}

static void poll_input(void)
{
    int is_down, keycode;
    while (port_poll_key(&is_down, &keycode)) {
        if (process_keycode(is_down, keycode) && pc && pc->kbd)
            ps2_put_keycode(pc->kbd, is_down, keycode);
    }

    int16_t dx, dy;
    int8_t dz;
    uint8_t buttons;
    while (port_poll_mouse(&dx, &dy, &dz, &buttons)) {
        if (pc && pc->mouse && !pc->paused && config_get_mouse()) {
            static MouseScaleState mouse_scale = { .setting = -1 };
            int16_t my = config_get_mouse_invert_y() ? -dy : dy;
            mouse_scale_delta(&mouse_scale, &dx, &my);
            ps2_mouse_event(pc->mouse, dx, my, dz, buttons);
        }
    }

    int jx, jy;
    uint8_t jb;
    if (pc && !pc->paused && config_get_usb_joystick() && port_poll_joystick(&jx, &jy, &jb))
        gameport_set(jx, jy, jb);
}

static void platform_poll(void *opaque)
{
    (void)opaque;
}

/*===========================================================================
 * Configuration
 *=========================================================================*/

static void load_default_config(void)
{
    memset(&config, 0, sizeof(config));
    config.mem_size = (long)FRANK_DEFAULT_MEM_MB << 20;
    config.vga_mem_size = (long)FRANK_DEFAULT_VGA_MEM_KB << 10;
    config.cpu_gen = 4;
    config.fpu = 1;
    config.width = FB_W;
    config.height = FB_H;
    config.bios = "bios.bin";
    config.vga_bios = "vgabios.bin";
    config.redirector = 1;
}

static char *read_text_file(const char *path)
{
    FIL fp;
    if (f_open(&fp, path, FA_READ) != FR_OK)
        return NULL;
    FSIZE_t size = f_size(&fp);
    char *content = malloc(size + 1);
    UINT n = 0;
    if (content && f_read(&fp, content, size, &n) == FR_OK) {
        content[n] = '\0';
    } else {
        free(content);
        content = NULL;
    }
    f_close(&fp);
    return content;
}

/*
 * The per-platform file written by the settings menu
 * (/.config/386/RPI4/config.ini) wins; 386/config.ini is the fallback
 * documented in the README and shipped in the repository.
 */
static const char *config_path_used = NULL;

static int load_config_from_sd(void)
{
    static const char *paths[] = { CONFIG_FILE_PATH, "386/config.ini" };
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        char *content = read_text_file(paths[i]);
        if (!content)
            continue;
        ini_parse_string(content, parse_conf_ini, &config);
        ini_parse_string(content, parse_frank_386_ini, NULL);
        free(content);
        config_path_used = paths[i];
        /* The Pi 4 drives the screen itself: ignore RP2350 display sizes. */
        config.width = FB_W;
        config.height = FB_H;
        return 0;
    }
    return -1;
}

/*===========================================================================
 * Initialisation
 *=========================================================================*/

static void show_welcome_screen(void)
{
    int wx = 14, wy = 6, ww = 51, wh = 14;
    osd_clear();
    osd_draw_box(wx, wy, ww, wh, OSD_ATTR_BORDER);
    osd_fill(wx + 1, wy + 1, ww - 2, wh - 2, ' ', OSD_ATTR_NORMAL);
    osd_print_center(wy + 3, "FRANK 386", OSD_ATTR(OSD_YELLOW, OSD_BLUE));
    char line[64];
    snprintf(line, sizeof(line), "Version %d.%02d", FRANK_386_VERSION_MAJOR, FRANK_386_VERSION_MINOR);
    osd_print_center(wy + 5, line, OSD_ATTR_NORMAL);
    osd_print_center(wy + 6, "Port by Mikhail Matveev & DnCraptor", OSD_ATTR_NORMAL);
    osd_print_center(wy + 7, "https://github.com/rh1tech/frank-386", OSD_ATTR_NORMAL);
    snprintf(line, sizeof(line), "RAM: %ld MB, VRAM: %ld KB",
             config.mem_size >> 20, (long)(pc ? pc->vga_mem_size >> 10 : 0));
    osd_print_center(wy + 9, line, OSD_ATTR(OSD_LIGHTCYAN, OSD_BLUE));
    snprintf(line, sizeof(line), "Platform: %s", port_platform_name());
    osd_print_center(wy + 10, line, OSD_ATTR(OSD_LIGHTGREEN, OSD_BLUE));
    osd_print_center(wy + 12, "Win+F12: disks   Win+F11: settings", OSD_ATTR_NORMAL);
    osd_show();

    for (int frame = 0; frame < 300; frame++) {   /* ~3 s, any key skips */
        int is_down = 0, keycode = 0;
        bool skip = false;
        while (port_poll_key(&is_down, &keycode))
            if (is_down) skip = true;
        if (skip) break;
        osd_draw_plasma_background(frame * 3, wx, wy, ww, wh);
        present_frame();
        sleep_ms(10);
    }
    osd_hide();
}

bool frank_init(void)
{
    printf("\n============================================\n");
    printf("  FRANK 386 v%d.%02d - %s port\n", FRANK_386_VERSION_MAJOR,
           FRANK_386_VERSION_MINOR, port_platform_name());
    printf("============================================\n");

    video_init();

    FRESULT res = f_mount(&fatfs, "", 1);
    if (res != FR_OK) {
        char detail[48];
        snprintf(detail, sizeof(detail), "FatFS error code: %d", res);
        show_error_screen(" SD Card Error ", "Failed to mount the SD card (FAT32).", detail);
    }
    DIR dir;
    if (f_opendir(&dir, "386") != FR_OK)
        show_error_screen(" Missing Directory ", "Directory '386/' not found on SD card.",
                          "Create it and add config.ini, bios.bin, vgabios.bin");
    f_closedir(&dir);

    load_default_config();
    if (load_config_from_sd() != 0)
        show_warning_screen(" Warning ", "config.ini not found, using defaults.", 1500);
    else
        printf("Config: %s\n", config_path_used);

    printf("Guest: %ld MB RAM, %ld KB VRAM, CPU %d86, FPU %s\n",
           config.mem_size >> 20, config.vga_mem_size >> 10, config.cpu_gen,
           config.fpu ? "on" : "off");

    pc = pc_new(vga_redraw, platform_poll, NULL, (u8 *)framebuffer, &config);
    if (!pc)
        show_error_screen(" Out of memory ", "Failed to create the PC instance.", NULL);
    pc->paused = 0;

    diskui_init();
    settingsui_init();

    config_set_cpu_gen(config.cpu_gen);
    config_set_fpu(config.fpu);
    config_set_redirector(config.redirector);
    config_clear_changes();

    pc->pcspk_enabled   = config_get_pcspeaker();
    pc->adlib_enabled   = config_get_adlib();
    pc->sb16_enabled    = config_get_soundblaster();
    pc->tandy_enabled   = config_get_tandy();
    pc->covox_enabled   = config_get_covox();
    pc->mpu401_enabled  = config_get_mpu401();
    pc->dss_enabled     = config_get_dss();
    pc->mouse_enabled   = config_get_mouse() || config_get_nes_mouse();
    pc->joystick_enabled = config_get_nes_joystick() || config_get_usb_joystick();

    audio_set_volume(config_get_volume());
    audio_set_enabled(true);

    if (!config.bios || !config.bios[0])
        show_error_screen(" Missing BIOS ", "No BIOS file specified in config.",
                          "Add bios=filename to config.ini");
    {
        char bios_path[256];
        FIL fp;
        snprintf(bios_path, sizeof(bios_path), "386/%s", config.bios);
        if (f_open(&fp, bios_path, FA_READ) != FR_OK) {
            char detail[80];
            snprintf(detail, sizeof(detail), "File: %s", bios_path);
            show_error_screen(" Missing BIOS ", "BIOS file not found.", detail);
        }
        f_close(&fp);
    }

    load_bios_and_reset(pc);

#ifndef FRANK_NO_WELCOME
    show_welcome_screen();
#endif
    return true;
}

/*===========================================================================
 * Main loop
 *=========================================================================*/

void frank_run(void)
{
    uint64_t last_present = 0;
    uint64_t last_poll = 0;

    for (;;) {
        uint64_t now = port_time_us();

        if (pc->paused) {
            poll_input();
            if (diskui_is_open())
                diskui_animate();
            else if (settingsui_is_open())
                settingsui_animate();
            present_frame();
            sleep_ms(16);
            goto housekeeping;
        }

#ifdef AUTOTYPE_STR
        autotype_tick();
#endif
        for (int i = 0; i < 8; i++)
            pc_step(pc);

        if (now - last_poll >= 2000) {          /* input every 2 ms */
            last_poll = now;
            poll_input();
        }

        /* vga_refresh() runs inside pc_step() on each emulated vblank
         * (~60 Hz) and sets frame_dirty through vga_redraw(). */
        if ((frame_dirty || pc->full_update) && now - last_present >= 16000) {
            last_present = now;
            present_frame();
        }

housekeeping:
        if (pc->reset_request) {
            pc->reset_request = 0;
            load_bios_and_reset(pc);
        }
        if (settingsui_restart_requested()) {
            settingsui_clear_restart();
            printf("Settings changed - restarting\n");
            port_disk_sync();
            port_reboot();
        }
        if (pc->shutdown_state) {
            printf("Guest requested shutdown\n");
            port_disk_sync();
            port_reboot();
        }
#ifdef FRANK_RUN_HOOK
        FRANK_RUN_HOOK();
#endif
    }
}

PC *frank_get_pc(void) { return pc; }

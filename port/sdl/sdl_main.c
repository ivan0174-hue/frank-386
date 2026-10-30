/*
 * FRANK 386 – Raspberry Pi 4 port
 * SDL2 backend.
 *
 * Runs the very same front-end as the bare-metal build, but on top of
 * Linux: useful on Raspberry Pi OS (no reflashing needed) and on a PC
 * for development.  The "SD card" is a FAT32 disk image file whose
 * layout is identical to the real card (386/, .config/...).
 *
 *   frank386 [sdcard.img]
 *
 * Environment variables (all optional):
 *   FRANK_SCALE=N          window scale factor (default 2)
 *   FRANK_FULLSCREEN=1     start full screen
 *   FRANK_SCREENSHOT=t1,t2 save shot-<t>.bmp after t seconds (testing)
 *   FRANK_EXIT_AFTER=t     quit after t seconds (testing)
 *   SDL_VIDEODRIVER=dummy  run headless
 */
#define _GNU_SOURCE
#include <SDL.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>

#include "port_hal.h"
#include "frank_main.h"
#include "hid_keymap.h"
#include "audio.h"

/* ---------------------------------------------------------------- time */

static struct timespec t0;

uint64_t port_time_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)(ts.tv_sec - t0.tv_sec) * 1000000ull +
           (uint64_t)((ts.tv_nsec - t0.tv_nsec) / 1000);
}

void port_sleep_us(uint64_t us)
{
    usleep((useconds_t)us);
}

void port_reboot(void)
{
    printf("Reboot requested - exiting (restart the program)\n");
    port_disk_sync();
    SDL_Quit();
    exit(0);
}

const char *port_platform_name(void)
{
    return "Linux/SDL2";
}

/* --------------------------------------------------------------- disk */

static int disk_fd = -1;
static uint32_t disk_sectors;
static const char *disk_path = "sdcard.img";

bool port_disk_init(void)
{
    disk_fd = open(disk_path, O_RDWR);
    if (disk_fd < 0) {
        perror(disk_path);
        return false;
    }
    struct stat st;
    fstat(disk_fd, &st);
    disk_sectors = (uint32_t)(st.st_size / 512);
    printf("SD image: %s (%u sectors)\n", disk_path, disk_sectors);
    return true;
}

bool port_disk_read(uint32_t sector, uint32_t count, void *buf)
{
    size_t len = (size_t)count * 512;
    return pread(disk_fd, buf, len, (off_t)sector * 512) == (ssize_t)len;
}

bool port_disk_write(uint32_t sector, uint32_t count, const void *buf)
{
    size_t len = (size_t)count * 512;
    return pwrite(disk_fd, buf, len, (off_t)sector * 512) == (ssize_t)len;
}

uint32_t port_disk_sector_count(void) { return disk_sectors; }

void port_disk_sync(void)
{
    if (disk_fd >= 0)
        fsync(disk_fd);
}

/* -------------------------------------------------------------- video */

#define OUT_W 640
#define OUT_H 480

static SDL_Window *window;
static SDL_Renderer *renderer;
static SDL_Texture *texture;
static int tex_w, tex_h;
static double shot_times[16];
static int shot_count, shot_next;
static double exit_after;

static void save_screenshot(const uint32_t *fb, int stride, int sx, int sy, int sw, int sh, double t)
{
    SDL_Surface *s = SDL_CreateRGBSurfaceWithFormat(0, sw, sh, 32, SDL_PIXELFORMAT_XRGB8888);
    if (!s) return;
    for (int y = 0; y < sh; y++)
        memcpy((uint8_t *)s->pixels + y * s->pitch, fb + (size_t)(sy + y) * stride + sx, (size_t)sw * 4);
    char name[64];
    snprintf(name, sizeof(name), "shot-%g.bmp", t);
    SDL_SaveBMP(s, name);
    SDL_FreeSurface(s);
    printf("Screenshot %s (%dx%d)\n", name, sw, sh);
}

void port_video_present(const uint32_t *fb, int stride, int sx, int sy, int sw, int sh)
{
    double now = port_time_us() / 1e6;
    while (shot_next < shot_count && now >= shot_times[shot_next]) {
        save_screenshot(fb, stride, sx, sy, sw, sh, shot_times[shot_next]);
        shot_next++;
    }
    if (exit_after > 0 && now >= exit_after) {
        port_disk_sync();
        SDL_Quit();
        exit(0);
    }
    if (!renderer)
        return;
    if (!texture || tex_w != sw || tex_h != sh) {
        if (texture) SDL_DestroyTexture(texture);
        texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
                                    SDL_TEXTUREACCESS_STREAMING, sw, sh);
        tex_w = sw;
        tex_h = sh;
    }
    SDL_UpdateTexture(texture, NULL, fb + (size_t)sy * stride + sx, stride * 4);
    SDL_RenderClear(renderer);
    /* The logical size is 4:3, so every PC mode is shown with CRT proportions. */
    SDL_RenderCopy(renderer, texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

/* -------------------------------------------------------------- input */

#define KEYQ 256
static struct { int down, code; } keyq[KEYQ];
static int kq_head, kq_tail;
static int mouse_dx, mouse_dy, mouse_dz, mouse_btn, mouse_btn_last, mouse_moved;
static bool grabbed;

static void key_push(int down, int code)
{
    int n = (kq_head + 1) % KEYQ;
    if (n == kq_tail) return;
    keyq[kq_head].down = down;
    keyq[kq_head].code = code;
    kq_head = n;
}

static void set_grab(bool g)
{
    grabbed = g;
    SDL_SetRelativeMouseMode(g ? SDL_TRUE : SDL_FALSE);
    SDL_SetWindowTitle(window, g ? "FRANK 386 (Ctrl+F10 releases the mouse)"
                                 : "FRANK 386 (click to capture the mouse)");
}

static void pump_events(void)
{
    SDL_Event ev;
    while (SDL_PollEvent(&ev)) {
        switch (ev.type) {
        case SDL_QUIT:
            port_disk_sync();
            SDL_Quit();
            exit(0);
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            if (ev.key.repeat)
                break;
            SDL_Scancode sc = ev.key.keysym.scancode;
            if (ev.type == SDL_KEYDOWN && sc == SDL_SCANCODE_F10 &&
                (ev.key.keysym.mod & KMOD_CTRL) && window) {
                set_grab(false);
                break;
            }
            if (ev.type == SDL_KEYDOWN && sc == SDL_SCANCODE_RETURN &&
                (ev.key.keysym.mod & KMOD_ALT) && window) {
                Uint32 fs = SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN_DESKTOP;
                SDL_SetWindowFullscreen(window, fs ? 0 : SDL_WINDOW_FULLSCREEN_DESKTOP);
                break;
            }
            int code = sc < 256 ? hid_to_linux_keycode((uint8_t)sc) : 0;
            if (code)
                key_push(ev.type == SDL_KEYDOWN, code);
            break;
        }
        case SDL_MOUSEBUTTONDOWN:
            if (!grabbed && window) {
                set_grab(true);
                break;
            }
            /* fallthrough */
        case SDL_MOUSEBUTTONUP: {
            int bit = ev.button.button == SDL_BUTTON_LEFT ? 1 :
                      ev.button.button == SDL_BUTTON_RIGHT ? 2 :
                      ev.button.button == SDL_BUTTON_MIDDLE ? 4 : 0;
            if (ev.type == SDL_MOUSEBUTTONDOWN) mouse_btn |= bit;
            else mouse_btn &= ~bit;
            mouse_moved = 1;
            break;
        }
        case SDL_MOUSEMOTION:
            if (grabbed) {
                mouse_dx += ev.motion.xrel;
                mouse_dy += ev.motion.yrel;
                mouse_moved = 1;
            }
            break;
        case SDL_MOUSEWHEEL:
            mouse_dz -= ev.wheel.y;
            mouse_moved = 1;
            break;
        }
    }
}

bool port_poll_key(int *is_down, int *keycode)
{
    if (kq_tail == kq_head)
        pump_events();
    if (kq_tail == kq_head)
        return false;
    *is_down = keyq[kq_tail].down;
    *keycode = keyq[kq_tail].code;
    kq_tail = (kq_tail + 1) % KEYQ;
    return true;
}

bool port_poll_mouse(int16_t *dx, int16_t *dy, int8_t *dz, uint8_t *buttons)
{
    if (!mouse_moved && mouse_btn == mouse_btn_last)
        return false;
    *dx = (int16_t)mouse_dx;
    *dy = (int16_t)mouse_dy;
    *dz = (int8_t)mouse_dz;
    *buttons = (uint8_t)mouse_btn;
    mouse_dx = mouse_dy = mouse_dz = 0;
    mouse_moved = 0;
    mouse_btn_last = mouse_btn;
    return true;
}

bool port_poll_joystick(int *x, int *y, uint8_t *buttons)
{
    (void)x; (void)y; (void)buttons;
    return false;
}

/* -------------------------------------------------------------- audio */

static void sdl_audio_cb(void *ud, Uint8 *stream, int len)
{
    (void)ud;
    audio_render((int16_t *)stream, len / 4);
}

/* --------------------------------------------------------------- main */

int main(int argc, char **argv)
{
    clock_gettime(CLOCK_MONOTONIC, &t0);
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1)
        disk_path = argv[1];

    const char *shots = getenv("FRANK_SCREENSHOT");
    if (shots) {
        char *dup = strdup(shots), *save = NULL;
        for (char *tok = strtok_r(dup, ",", &save); tok && shot_count < 16;
             tok = strtok_r(NULL, ",", &save))
            shot_times[shot_count++] = atof(tok);
        free(dup);
    }
    if (getenv("FRANK_EXIT_AFTER"))
        exit_after = atof(getenv("FRANK_EXIT_AFTER"));

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_EVENTS) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }

    const char *vd = SDL_GetCurrentVideoDriver();
    if (!vd || strcmp(vd, "dummy") != 0) {
        int scale = getenv("FRANK_SCALE") ? atoi(getenv("FRANK_SCALE")) : 2;
        if (scale < 1) scale = 1;
        Uint32 flags = SDL_WINDOW_RESIZABLE;
        if (getenv("FRANK_FULLSCREEN")) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
        window = SDL_CreateWindow("FRANK 386", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                  OUT_W * scale, OUT_H * scale, flags);
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer)
            renderer = SDL_CreateRenderer(window, -1, 0);
        SDL_RenderSetLogicalSize(renderer, OUT_W, OUT_H);
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");
        set_grab(false);
    }

    SDL_AudioSpec want = {0}, have;
    want.freq = SOUND_FREQUENCY;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = 1024;
    want.callback = sdl_audio_cb;
    SDL_AudioDeviceID adev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
    if (!adev)
        printf("Audio disabled: %s\n", SDL_GetError());

    if (!frank_init())
        return 1;
    if (adev)
        SDL_PauseAudioDevice(adev, 0);
    frank_run();
    return 0;
}

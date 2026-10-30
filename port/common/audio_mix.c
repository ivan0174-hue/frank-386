/*
 * FRANK 386 – Raspberry Pi 4 port
 * Audio mixer.
 *
 * Same device mix as drivers/audio/audio.c (timer_callback) on the
 * RP2350, but block-based: the backend asks for N stereo frames and we
 * produce them, advancing a sample-accurate audio clock that also drives
 * the emulated OPL batch timer (see timers.c).
 */
#include <stdint.h>
#include <stdbool.h>
#include "pc.h"
#include "audio.h"
#include "dss.h"
#include "pico/time.h"

extern PC *pc;

/* 0 = full volume, 16 = silence (right-shift amount), as on RP2350. */
static volatile uint8_t volume_shift = 1;
static uint8_t saved_shift = 1;
static int boot_output = AUDIO_OUTPUT_AUTO;

void audio_set_boot_output(int output)
{
    if (output >= AUDIO_OUTPUT_AUTO && output <= AUDIO_OUTPUT_I2S)
        boot_output = output;
}

int audio_get_boot_output(void) { return boot_output; }

void audio_set_enabled(bool v)
{
    if (v) {
        volume_shift = saved_shift;
    } else {
        saved_shift = volume_shift;
        volume_shift = 16;
    }
}

void audio_set_volume(uint8_t vol)
{
    if (vol > 16) vol = 16;
    /* Settings menu volume 0..16 (16 = loudest). */
    saved_shift = 16 - vol;
    if (volume_shift != 16)
        volume_shift = saved_shift;
}

uint8_t audio_get_volume(void) { return 16 - saved_shift; }

void audio_init(void) { }

static inline int16_t clamp16(int v)
{
    if (v > 32767) return 32767;
    if (v < -32768) return -32768;
    return (int16_t)v;
}

/*
 * DC blocker (one-pole high-pass, ~3.5 Hz at 44.1 kHz).
 *
 * Real sound cards are AC-coupled, so a DAC parked at a non-zero value is
 * silent.  The emulated ones are not: the BIOS probes the parallel port by
 * writing 0xAA to it, which leaves the Covox DAC at a constant offset.
 * That offset is inaudible on its own, but every time the output stream
 * pauses (start-up, menus, a late audio buffer) the signal jumps between
 * the offset and zero and you hear a loud repeating click.  Removing DC
 * here behaves like the coupling capacitor of the real hardware.
 *
 *   y[n] = x[n] - x[n-1] + R * y[n-1],   R = 1 - 2^-11
 */
typedef struct { int32_t x1; int32_t y1; } DCBlock;

static inline int32_t dc_block(DCBlock *f, int32_t x)
{
    /* y1 is kept with 8 fractional bits to avoid limit cycles */
    /* round the decay away from zero so the filter settles exactly at 0 */
    int32_t decay = f->y1 > 0 ? (f->y1 + 2047) >> 11 : f->y1 >> 11;
    int32_t y = ((x - f->x1) << 8) + f->y1 - decay;
    f->x1 = x;
    f->y1 = y;
    return y >> 8;
}

static DCBlock dc_l, dc_r;

/* Audio clock in 1/SOUND_FREQUENCY steps, converted to µs for the timers. */
static uint64_t audio_frames;

void audio_render(int16_t *out, int frames)
{
    static uint32_t dss_phase = 0;
    static int dss_v = 0;
    static int timer_phase = 0;
    const int frames_per_ms = SOUND_FREQUENCY / 1000;   /* 44 */

    PC *p = pc;
    if (!p) {
        memset(out, 0, (size_t)frames * 2 * sizeof(int16_t));
        return;
    }

    for (int i = 0; i < frames; i++) {
        /* Fire the OPL batch renderer once per millisecond of audio. */
        if (++timer_phase >= frames_per_ms) {
            timer_phase = 0;
            port_timers_service(audio_frames * 1000000ull / SOUND_FREQUENCY);
        }
        audio_frames++;

        if (p->dss_enabled) {
            dss_phase += 7000;
            if (dss_phase >= SOUND_FREQUENCY) {
                dss_phase -= SOUND_FREQUENCY;
                dss_v = dss_sample();
            }
        }

        int l = 0, r = 0, beep = 0;
        if (p->pcspk_enabled)
            beep = pcspk_sample(p->pcspk);
        if (p->covox_enabled && p->covox_sample) {
            int s = ((int)p->covox_sample - 127) << 8;
            l += s; r += s;
        }
        if (p->tandy_enabled) {
            int s = sn76489_sample();
            l += s; r += s;
        }
        if (p->mpu401_enabled) {
            int s = midi_sample();
            l += s; r += s;
        }
        if (p->adlib_enabled) {
            int s = adlib_getsample(p->adlib);
            l += s; r += s;
        }
        if (p->sb16_enabled)
            sb16_getsample(p->sb16, &r, &l);
        if (p->dss_enabled) {
            l += dss_v; r += dss_v;
        }
        if (beep) {
            /* PC speaker is a 1-bit device; mix it at a moderate level. */
            l += 8000; r += 8000;
        }

        l = dc_block(&dc_l, l);
        r = dc_block(&dc_r, r);

        const uint8_t sh = volume_shift;
        out[2 * i + 0] = sh >= 16 ? 0 : clamp16(l >> sh);
        out[2 * i + 1] = sh >= 16 ? 0 : clamp16(r >> sh);
    }
}

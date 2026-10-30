/*
 * FRANK 386 – Raspberry Pi 4 port: audio front-end.
 * Replaces drivers/audio/audio.h (I2S/PWM on RP2350).
 */
#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define AUDIO_OUTPUT_AUTO 0
#define AUDIO_OUTPUT_PWM  1   /* Pi 4: 3.5 mm headphone jack */
#define AUDIO_OUTPUT_I2S  2   /* Pi 4: HDMI audio            */

void audio_set_boot_output(int output);
int  audio_get_boot_output(void);
void audio_set_enabled(bool);
void audio_set_volume(uint8_t);
uint8_t audio_get_volume(void);
void audio_init(void);

/*
 * Render `frames` stereo frames (interleaved L/R int16) at SOUND_FREQUENCY.
 * Called by the backend's audio thread / core.  Advances the audio clock
 * that drives the emulated OPL timer.
 */
struct PC_s;
void audio_render(int16_t *out, int frames);

#ifdef __cplusplus
}
#endif

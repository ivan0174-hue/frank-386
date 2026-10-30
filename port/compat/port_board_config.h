/*
 * FRANK 386 – Raspberry Pi 4 port: replaces the RP2350 GPIO board
 * definitions.  The Pi 4 talks to HDMI, USB and the SD card through
 * its own controllers, so no pin assignments are needed here.
 */
#ifndef FRANK_PORT_BOARD_CONFIG_H
#define FRANK_PORT_BOARD_CONFIG_H

#define HAS_AUDIO_I2S 0
#define HAS_AUDIO_PWM 0

/* Values shown in the settings menu; the Pi 4 does not overclock at runtime. */
#ifndef CPU_CLOCK_MHZ
#define CPU_CLOCK_MHZ 1500
#endif
#ifndef PSRAM_MAX_FREQ_MHZ
#define PSRAM_MAX_FREQ_MHZ 0
#endif
#ifndef FLASH_MAX_FREQ_MHZ
#define FLASH_MAX_FREQ_MHZ 0
#endif

#ifndef CONFIG_PLATFORM_NAME
#define CONFIG_PLATFORM_NAME "RPI4"
#endif

#define CONFIG_CPU_DIR      "/.config/386"
#define CONFIG_PLATFORM_DIR CONFIG_CPU_DIR "/" CONFIG_PLATFORM_NAME
#define CONFIG_FILE_PATH    CONFIG_PLATFORM_DIR "/config.ini"
#define VIDEO_MARKER_VGA    CONFIG_PLATFORM_DIR "/force_vga"
#define VIDEO_MARKER_HDMI   CONFIG_PLATFORM_DIR "/force_dvi"
#define AUDIO_MARKER_PWM    CONFIG_PLATFORM_DIR "/force_pwm"
#define AUDIO_MARKER_I2S    CONFIG_PLATFORM_DIR "/force_i2s"

#endif

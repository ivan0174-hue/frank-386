FRANK 386 – Raspberry Pi 4 bare-metal SD card
=============================================

1. Format a micro-SD card with ONE FAT32 partition (MBR).
2. Copy EVERYTHING in this folder to the root of that partition:
      start4.elf  fixup4.dat  bcm2711-*.dtb  armstub8-rpi4.bin
      kernel8-rpi4.img  config.txt  cmdline.txt  386/
3. Put your disk images in 386/ and list them in 386/config.ini.
4. Insert the card, connect HDMI + USB keyboard (and mouse), power on.

cmdline.txt options (single line, space separated):
  width=1280 height=720   frame buffer size (the GPU scales it to the monitor)
  sounddev=sndpwm         force 3.5 mm jack audio
  sounddev=sndhdmi        force HDMI audio (default: HDMI, jack as fallback)
  loglevel=4              serial log verbosity (0..5), 115200 8N1 on GPIO14

Hotkeys: Win+F12 = disk manager, Win+F11 = settings.

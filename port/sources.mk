# FRANK 386 – Raspberry Pi 4 port: source list shared by both backends.
# FRANK_ROOT must point at the repository root.

VER_MAJOR := $(shell cut -d' ' -f1 $(FRANK_ROOT)/version.txt)
VER_MINOR := $(shell cut -d' ' -f2 $(FRANK_ROOT)/version.txt)

CORE_C := \
	src/i386.c src/fpu.c src/pc.c src/i8042.c src/i8254.c src/i8257.c src/i8259.c \
	src/vga.c src/disk.c src/ide.c src/fdd.c src/pci.c src/misc.c \
	src/adlib.c src/sb16.c src/pcspk.c src/sn76489.c src/dss.c src/gameport.c \
	src/emu8950/emu8950.c src/emu8950/emuadpcm.c \
	src/ini.c src/netredirect.c src/autotype.c \
	src/codeprofile.c src/bbprofile.c src/profile_subsys.c \
	src/diskui.c src/settingsui.c src/config_save.c src/stubs_rp2350.c \
	drivers/fatfs/ff.c drivers/fatfs/ffsystem.c drivers/fatfs/ffunicode.c drivers/fatfs/f_util.c \
	drivers/vga/font8x16.c \
	port/common/frank_main.c port/common/audio_mix.c port/common/timers.c \
	port/common/diskio_port.c port/common/osd_soft.c port/common/hid_keymap.c

CORE_CXX := src/emu8950/slot_render.cpp

CORE_DEFS := \
	-DFRANK_PORT=1 -DNO_NETWORK=1 -DSOUND_FREQUENCY=44100 \
	-DUSE_EMU8950_OPL -DEMU8950_SLOT_RENDER=1 -DEMU8950_NO_RATECONV=1 \
	-DEMU8950_NO_WAVE_TABLE_MAP=1 -DEMU8950_NO_TLL=1 -DEMU8950_NO_FLOAT=1 \
	-DEMU8950_NO_TIMER=1 -DEMU8950_NO_TEST_FLAG=1 -DEMU8950_SIMPLER_NOISE=1 \
	-DEMU8950_SHORT_NOISE_UPDATE_CHECK=1 -DEMU8950_LINEAR_SKIP=1 \
	-DEMU8950_LINEAR_END_OF_NOTE_OPTIMIZATION -DEMU8950_NO_PERCUSSION_MODE=1 \
	-DEMU8950_LINEAR=1 -DI386_ENABLE_INLINE=1 \
	-DFRANK_386_VERSION_MAJOR=$(VER_MAJOR) -DFRANK_386_VERSION_MINOR=$(VER_MINOR)

# port/compat must come first so the Pico SDK shims win.
CORE_INC := -include pico.h -I$(FRANK_ROOT)/port/compat -I$(FRANK_ROOT)/port/common \
	-I$(FRANK_ROOT)/src -I$(FRANK_ROOT)/drivers/fatfs -I$(FRANK_ROOT)/drivers/vga


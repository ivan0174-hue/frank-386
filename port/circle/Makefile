#
# FRANK 386 – Raspberry Pi 4 bare-metal kernel (Circle + newlib)
#
# Needs circle-stdlib configured for the Pi 4 in AArch64 mode:
#   ./configure -r 4 -p aarch64-none-elf- --kernel-max-size 16 \
#               -o ARM_ALLOW_MULTI_CORE -o SAVE_VFP_REGS_ON_IRQ
#   make
# then:  make CIRCLE_STDLIB=/path/to/circle-stdlib
# Output: kernel8-rpi4.img
#

CIRCLE_STDLIB ?= /opt/circle-stdlib
FRANK_ROOT ?= $(abspath ../..)

include $(CIRCLE_STDLIB)/Config.mk
include $(FRANK_ROOT)/port/sources.mk

OPTIMIZE   = -O3
C_STANDARD = -std=gnu11

# Object files: Circle builds every OBJS entry from a matching source
# beside it, so the emulator sources are compiled through vpath-free
# explicit rules into obj/.
EMU_OBJS := $(addprefix obj/,$(notdir $(CORE_C:.c=.o))) $(addprefix obj/,$(notdir $(CORE_CXX:.cpp=.o)))
OBJS = circle_main.o kernel.o $(EMU_OBJS)

include $(CIRCLEHOME)/Rules.mk

INCLUDE  := $(CIRCLE_STDLIB_INCLUDES) -I $(FRANK_ROOT)/port/compat -I $(FRANK_ROOT)/port/common $(INCLUDE)
DEFINE   += -DSOUND_FREQUENCY=44100 $(EXTRA_DEFINES)

# The emulator core uses its own FatFs (drivers/fatfs) on top of EMMC, so
# Circle's FatFs addon and the network/QEMU libraries are left out.
LIBS := $(CIRCLE_STDLIB_INSTALL_DIR)/lib/libm.a \
	$(CIRCLE_STDLIB_INSTALL_DIR)/lib/libc.a \
	$(CIRCLE_STDLIB_INSTALL_DIR)/lib/libcirclenewlib.a \
	$(CIRCLEHOME)/addon/SDCard/libsdcard.a \
	$(CIRCLEHOME)/lib/usb/libusb.a \
	$(CIRCLEHOME)/lib/input/libinput.a \
	$(CIRCLEHOME)/lib/fs/libfs.a \
	$(CIRCLEHOME)/lib/sound/libsound.a \
	$(CIRCLEHOME)/lib/sched/libsched.a \
	$(CIRCLEHOME)/lib/libcircle.a

EMU_CFLAGS = $(CORE_INC) $(CORE_DEFS) -fno-strict-aliasing -Wno-unused-result
ifneq ($(AUTOTYPE),)
# Scripted keystrokes typed into the guest after boot (testing aid).
EMU_CFLAGS += -DAUTOTYPE_STR='"$(AUTOTYPE)"'
endif

# One explicit rule per emulator source (no vpath: src/main.c must not
# shadow this directory's sources).
define EMU_C_RULE
obj/$(notdir $(1:.c=.o)): $(FRANK_ROOT)/$(1)
	@mkdir -p obj
	@echo "  CC    $(1)"
	@$$(CC) $$(EMU_CFLAGS) $$(CFLAGS) $$(C_STANDARD) -Wno-all -c -o $$@ $$<
endef
define EMU_CXX_RULE
obj/$(notdir $(1:.cpp=.o)): $(FRANK_ROOT)/$(1)
	@mkdir -p obj
	@echo "  CPP   $(1)"
	@$$(CPP) $$(EMU_CFLAGS) $$(CPPFLAGS) -Wno-all -c -o $$@ $$<
endef
$(foreach f,$(CORE_C),$(eval $(call EMU_C_RULE,$(f))))
$(foreach f,$(CORE_CXX),$(eval $(call EMU_CXX_RULE,$(f))))

distclean: clean
	rm -rf obj

-include $(DEPS)

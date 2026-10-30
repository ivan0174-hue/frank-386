# FRANK 386 – Linux/SDL2 build (Raspberry Pi OS 64-bit or a desktop PC)
#   make            -> ./frank386
#   make AUTOTYPE="dir\r"   (optional scripted keystrokes, for testing)

FRANK_ROOT := $(abspath ../..)
include ../sources.mk

CC  ?= gcc
CXX ?= g++
OPT ?= -O2
BUILD := build

SDL_CFLAGS := $(shell sdl2-config --cflags)
SDL_LIBS   := $(shell sdl2-config --libs)

CFLAGS   += $(OPT) -g -std=gnu11 -fno-strict-aliasing -Wno-unused-result $(CORE_DEFS) $(CORE_INC) $(SDL_CFLAGS)
CXXFLAGS += $(OPT) -g -std=gnu++17 -fno-strict-aliasing $(CORE_DEFS) $(CORE_INC)
ifneq ($(AUTOTYPE),)
CFLAGS += -DAUTOTYPE_STR='"$(AUTOTYPE)"'
endif
ifneq ($(NO_WELCOME),)
CFLAGS += -DFRANK_NO_WELCOME=1
endif

SRCS_C := $(CORE_C) port/sdl/sdl_main.c
OBJS := $(addprefix $(BUILD)/,$(SRCS_C:.c=.o)) $(addprefix $(BUILD)/,$(CORE_CXX:.cpp=.o))

frank386: $(OBJS)
	$(CXX) -o $@ $(OBJS) $(SDL_LIBS) -lm

$(BUILD)/%.o: $(FRANK_ROOT)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: $(FRANK_ROOT)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD) frank386

.PHONY: clean

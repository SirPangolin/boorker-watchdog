# Boorker OLED Emulator — renders firmware screens in SDL window
#
# Usage:
#   make          Build the emulator
#   make run      Build and run
#   make clean    Remove build artifacts
#
# Prerequisites:
#   sudo apt install libsdl2-dev
#   git submodule update --init (for u8g2)

# Paths
U8G2_SRC     = u8g2/csrc
FIRMWARE_SRC = ../../firmware/components/status_display/src
FIRMWARE_INC = ../../firmware/components

# Compiler — local shims (.) override ESP-IDF headers, then real firmware headers
CC = gcc
CFLAGS = -g -Wall -Wno-unused-function \
         -I. \
         -I$(U8G2_SRC) \
         -I$(FIRMWARE_INC)/system_state/include \
         -I$(FIRMWARE_INC)/event_bus/include \
         -I$(FIRMWARE_INC)/secrets/include \
         $(shell sdl2-config --cflags) \
         -DCONFIG_STATUS_DISPLAY_ENABLED=1

LDFLAGS = $(shell sdl2-config --libs) -lm

# u8g2 core sources (same files as the ESP-IDF managed component uses)
U8G2_CORE = $(wildcard $(U8G2_SRC)/u8g2_*.c) $(wildcard $(U8G2_SRC)/u8x8_*.c)

# u8g2 SDL virtual display (the HAL that replaces our I2C HAL)
U8G2_SDL_DIR = u8g2/sys/sdl/common
U8G2_SDL = $(U8G2_SDL_DIR)/u8x8_d_sdl_128x64.c $(U8G2_SDL_DIR)/u8x8_sdl_key.c

# Firmware screen files (compiled directly — no copies)
SCREEN_SRC = $(FIRMWARE_SRC)/display_screens.c \
             $(FIRMWARE_SRC)/display_logo.c

# Emulator-specific files
EMU_SRC = main.c mock_esp.c

# All sources
SRCS = $(U8G2_CORE) $(U8G2_SDL) $(SCREEN_SRC) $(EMU_SRC)
OBJS = $(patsubst %.c,build/%.o,$(notdir $(SRCS)))

TARGET = oled_emulator

# Build rules
all: build $(TARGET)

build:
	@mkdir -p build

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(OBJS) $(LDFLAGS) -o $@

# Generic compile rule — find source in various directories
VPATH = $(U8G2_SRC) $(U8G2_SDL_DIR) $(FIRMWARE_SRC) .

build/%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET)

clean:
	rm -rf build $(TARGET)

.PHONY: all clean run

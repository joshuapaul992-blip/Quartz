# Quartz - a from-scratch GUI for FreeDOS
#
# Cross-compiled with DJGPP (32-bit protected mode). Set DJGPP_PREFIX to the
# toolchain root (default /opt/djgpp).

DJGPP_PREFIX ?= /opt/djgpp
CC      := $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-gcc
STRIP   := $(DJGPP_PREFIX)/bin/i586-pc-msdosdjgpp-strip

WATT32  := third_party/watt32
CFLAGS  := -O2 -Wall -Isrc -I$(WATT32)/inc
LDFLAGS := -L$(WATT32)/lib
LDLIBS  := -lwatt

SRC := src/main.c \
       src/gfx/vbe.c src/gfx/draw.c src/gfx/font.c \
       src/input/mouse.c src/input/keyboard.c \
       src/wm/wm.c src/wm/widget.c \
       src/net/net.c \
       src/audio/sb16.c \
       src/hal/pci.c src/sys/serial.c \
       src/drivers/usb/xhci.c

OBJ := $(patsubst src/%.c,build/%.o,$(SRC))

APP := build/quartz.exe

.PHONY: all image run shot clean toolinfo

all: $(APP)

build/%.o: src/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@

$(APP): $(OBJ)
	$(CC) $(LDFLAGS) $(OBJ) $(LDLIBS) -o $@
	$(STRIP) $@

# Build bootable floppy + data disk carrying quartz.exe
image: $(APP)
	tools/mkdisk.sh --app $(APP) --run QUARTZ.EXE

# Build images and launch QEMU, capturing a screenshot to dist/screen.png
run: image
	python3 tools/run_qemu.py --wait 16 --out dist/screen.png

shot: run
	@echo "screenshot -> dist/screen.png"

toolinfo:
	@echo "CC = $(CC)"; $(CC) --version 2>/dev/null | head -1 || echo "toolchain not built yet"

clean:
	rm -rf build dist/boot.img dist/disk.img dist/*.png

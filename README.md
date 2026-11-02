# Quartz

A from-scratch graphical environment for **FreeDOS**, targeting real hardware
(daily-usable retro system). Quartz is a 32-bit protected-mode desktop that
owns the screen through the **VESA VBE linear framebuffer** and sits on top of
proven DOS TSR drivers for mouse, networking, and audio.

## Target machine

| Part      | Device                        | Quartz path |
|-----------|-------------------------------|-------------|
| Firmware  | UEFI with CSM / Legacy        | legacy VBE BIOS is present |
| GPU       | NVIDIA GTX 1070 (Pascal)      | VESA VBE 2.0/3.0 LFB (dumb framebuffer, dirty-rect redraw) |
| Audio     | Realtek HD Audio              | SBEMU → SB16 emulation (Phase 4) |
| NIC       | Realtek RTL8169 (+ others)    | Crynwr packet driver + Watt-32 (Phase 3) |
| CPU       | Intel Core i7-8700            | 32-bit protected mode via CWSDPMI |

## Architecture

```
  +------------------------------------------------------+
  |  Quartz shell (DJGPP, 32-bit protected mode)         |
  |    window manager · widgets · apps                   |
  |    gfx core: VBE LFB · surfaces · ROM font           |
  +----+------------------+---------------+--------------+
       |                  |               |
   INT 10h/VBE        INT 33h          Watt-32 / SB16
   (video BIOS)       (ctmouse)        packet drv / SBEMU
       |                  |               |  (TSRs loaded at boot)
  +------------------------------------------------------+
  |                     FreeDOS kernel                    |
  +------------------------------------------------------+
```

- **Language/toolchain:** DJGPP (GCC for DOS) + CWSDPMI DPMI host.
- **Compositing:** everything is drawn into an off-screen 32-bpp back buffer;
  only dirty rectangles are copied to the linear framebuffer.
- **Portability:** the video mode is *enumerated at runtime*, so the same
  binary picks the best LFB mode on QEMU's stdvga and on the GTX 1070.

## Repository layout

```
src/gfx/      VBE driver, drawing primitives, ROM-font text
src/input/    INT 33h mouse, INT 16h keyboard
src/wm/       window manager + widgets (Phase 2)
src/main.c    desktop shell / event loop
tools/        mkdisk.sh (build images), run_qemu.py (boot + screenshot),
              vbeprobe.asm (VBE capability probe)
third_party/  FreeDOS boot seed, CWSDPMI (inputs for image building)
dist/         built images + screenshots (gitignored)
```

## Building

Requires the DJGPP cross-compiler at `/opt/djgpp` (see `docs/TOOLCHAIN.md`),
plus `qemu-system-i386`, `mtools`, and `dosfstools`.

```bash
make            # cross-compile build/quartz.exe
make image      # assemble dist/boot.img + dist/disk.img
make run        # boot in QEMU and capture dist/screen.png
```

## Status

See `docs/STATUS.md`. Phase 1 (graphics core) code is complete; the dev loop
(QEMU boot + framebuffer screenshot) and VESA LFB availability are validated.

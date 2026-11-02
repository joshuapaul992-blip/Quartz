# Quartz — build status

_Last updated: 2026-11-02_

## Done & validated

- **Dev environment**: Ubuntu container with QEMU 8.2, mtools, dosfstools, nasm.
- **FreeDOS dev loop**: a trimmed FreeDOS 1.3 boot floppy chains to a
  partitioned FAT16 data disk (C:) that auto-runs the app. `run_qemu.py` boots
  headlessly and captures the framebuffer to PNG via the QEMU monitor.
  *Verified:* FreeDOS boots, C: mounts, `AUTORUN.BAT` runs the app.
- **VESA LFB confirmed**: `tools/vbeprobe.asm` enumerated LFB modes on QEMU
  stdvga including **1920×1080×32** (mode 0x192) at phys `0xFD000000`. This is
  the exact path the Quartz graphics core uses.
- **Project + build system**: `Makefile` (DJGPP cross-compile), `mkdisk.sh`
  (image assembly), `run_qemu.py` (boot + screenshot). Image pipeline verified
  end-to-end with a stand-in app.
- **Phase 1 code (written, pending compile)**:
  - `gfx/vbe.c` — VBE info/mode enumeration, best-fit LFB mode select, physical
    framebuffer mapping (DPMI), dirty-rect present (32/24 bpp).
  - `gfx/draw.c` — surfaces + clipped primitives (rect, line, bevel, blit w/ key).
  - `gfx/font.c` — VGA ROM 8×16 font fetched from the video BIOS at runtime.
  - `input/mouse.c` — INT 33h, resolution-independent relative motion.
  - `input/keyboard.c` — non-blocking INT 16h.
  - `main.c` — desktop shell: gradient wallpaper, menu bar + live clock, demo
    windows, mouse cursor with dirty-rect restore/redraw.

- **Phase 2 code (written + host-validated)**:
  - `wm/wm.c` — z-ordered windows each with their own surface; dirty-rectangle
    compositor into an off-screen scene; cursor overlay; title-bar dragging;
    focus/raise with correct re-render of the de-focused window; close boxes;
    menu bar + live clock; taskbar with active-window highlight.
  - `wm/widget.c` — label, button (press state + callback), checkbox (toggle),
    panel; painting + hit-testing.
  - Rewired `main.c` to open three app windows (About / Widgets Demo / Files).
- **Off-DOS render harness** (`tools/hostrender/`): the real wm/draw/widget
  code compiled with host GCC, driven by a scripted mouse, renders the actual
  Quartz desktop to PNG. Verified: window layout, overlap/occlusion, dragging,
  button click (counter increments), checkbox state, and single-window focus
  (confirmed by pixel sampling). Font is the real VGA ROM 8×16 glyph set dumped
  from QEMU over serial (`tools/getfont.asm`).
- **Static check**: all 8 DOS modules pass `gcc -fsyntax-only -Wall` against
  stub DJGPP headers (`tools/hoststubs/`), catching C errors pre-toolchain.

## Running on FreeDOS (toolchain unblocked)

- **DJGPP GCC 12.2.0** cross-compiler built from source at `/opt/djgpp`.
- `make` builds `build/quartz.exe` (DJGPP go32 extender) with **zero warnings**.
- **Quartz boots and renders on FreeDOS in QEMU**: real INT 10h VBE mode-set,
  DPMI physical-framebuffer mapping, `movedata` present, INT 10h ROM-font text,
  INT 33h mouse (ctmouse). Runtime-selected 1152×864×32 LFB. The on-DOS render
  matches the host preview exactly — Phase 1 + Phase 2 validated end-to-end on
  the actual OS.

## Phase 3 — networking (done, validated)

- Vendored prebuilt **DJGPP Watt-32** (`libwatt.a` + headers) + packet drivers
  (NE2000/PCnet/E1000). `src/net` brings up the stack (packet driver + DHCP) in
  text mode, then the GUI shows link status + address.
- Verified on FreeDOS in QEMU (NE2000 + SLIRP): **DHCP lease 10.0.2.15**,
  mask 255.255.255.0, DNS 10.0.2.3, shown in the Network window.
- For metal (RTL8169) a matching packet driver is still to be sourced (Phase 5).

## Next

1. **Phase 4 — audio**: SBEMU bridging SB16 → the Realtek HDA codec; GUI sound.
3. Richer WM/apps: keyboard focus, text-input widget, drop-down menus, dialogs.
4. **Phase 5 — hardware bring-up** on the i7-8700 / GTX 1070 / RTL8169 box:
   bootable USB, validate VBE modes, ctmouse with USB mouse, packet driver +
   DHCP, SBEMU on the real ALC codec.

## Toolchain (resolved)

The DJGPP build from source was initially blocked by the sandbox's
auto-permission classifier; after switching the session to acceptEdits it built
cleanly (binutils + GCC 12.2.0) at `/opt/djgpp`. See `docs/TOOLCHAIN.md`.

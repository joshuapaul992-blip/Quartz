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

## Blocked

- **DJGPP toolchain build** — the from-source build (binutils + GCC 12.2.0 from
  gnu.org / delorie.com mirrors) is blocked by the sandbox's auto-permission
  classifier ("Code from External"), because it executes third-party
  `configure`/`make`. Needs an explicit permission grant to proceed. Until then,
  `build/quartz.exe` cannot be produced and Phase 1 code is unverified.

## Next

1. Build the toolchain (pending permission) → `make run` → verify Phase 1
   renders in QEMU; fix any compile/runtime issues.
2. Vendor `ctmouse` (CuteMouse) for a working cursor in QEMU and on hardware.
3. Phase 2: window manager + widgets.

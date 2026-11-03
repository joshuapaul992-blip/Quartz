# Quartz bootable USB image

`dist/quartz-usb.img` is a bootable raw disk image: MBR + one active FAT16
partition with a real FreeDOS boot sector (installed via `FDISK /MBR` + `SYS`),
`KERNEL.SYS`, `COMMAND.COM`, and the Quartz runtime. Build it with:

```bash
make                                  # build build/quartz.exe
tools/mkusb.sh --app build/quartz.exe # -> dist/quartz-usb.img (256 MB)
```

## Write it to a USB stick

Linux (replace `/dev/sdX` with your stick — double-check, this erases it):

```bash
gunzip -k quartz-usb.img.gz           # if you received the .gz
sudo dd if=quartz-usb.img of=/dev/sdX bs=4M conv=fsync status=progress
```

Windows: use Rufus / balenaEtcher in "DD / raw image" mode.

## Boot it

- Enter firmware setup and enable **CSM / Legacy boot** (you confirmed this is
  available). UEFI-only has no VESA BIOS, which Quartz needs.
- Boot the USB as a **USB-HDD** device.
- FreeDOS loads, `AUTOEXEC.BAT` starts the mouse driver and launches Quartz.

## What works out of the box
- Graphics (VESA LFB — enumerated at runtime, picks the best 32-bpp mode).
- Mouse (ctmouse / INT 33h), keyboard, the full desktop/windows/widgets.
- Audio **if** an SB16-compatible path exists (see SBEMU below).

## Hardware add-ons (drop these on the stick's partition, then reboot)
`AUTOEXEC.BAT` already loads them automatically if present:

- **Audio on modern HD Audio (your Realtek codec):** copy `SBEMU.EXE` (+ its
  driver files) to the partition root. SBEMU traps SB16 I/O and plays it through
  the HDA/AC'97 codec, so Quartz's SB16 driver then produces sound. Source:
  the SBEMU project (crazii/SBEMU).
- **Networking on your RTL8169:** load the matching Realtek packet driver before
  Quartz, e.g. add a line `C:\RTSPKT.COM 0x60` to `AUTOEXEC.BAT` (bundled
  NE2000/PCnet/E1000 drivers are for other NICs / emulators). Then use the
  **Connect** button in the Network app (DHCP via `WATTCP.CFG`).

## Native USB mouse (xHCI)
Quartz now includes a native xHCI + USB-HID driver, so a **USB mouse (incl.
USB-3) works even when the BIOS doesn't emulate it as PS/2** — no "USB Legacy
Support" needed for the mouse. It enumerates the root-hub ports, skips the
keyboard, and polls the mouse via HID GET_REPORT. If it can't find/own the
controller it silently falls back to ctmouse/INT 33h, then keyboard control.
(USB keyboard + USB storage are not handled natively yet.)

## Mouse / input
- **Enable "USB Legacy Support" (USB keyboard/mouse emulation) in the firmware.**
  Without it, DOS can't see a USB mouse; with it, a USB mouse appears as PS/2 and
  the bundled `ctmouse` driver (loaded by AUTOEXEC) drives it.
- If the mouse still isn't captured, mouse movement can leak into the keyboard
  buffer. Quartz now ignores that (it never quits on ESC) so it won't exit on
  movement — but the cursor won't move until the mouse is seen by DOS.
- **Quit Quartz with Ctrl+Q.**
- **No working mouse?** Quartz falls back to keyboard control: **arrow keys move the cursor, Enter clicks, Ctrl+Q quits.**


## Troubleshooting
- Quartz composes internally at 32-bpp and converts to whatever depth the card's
  VESA mode actually is (16/24/32), clamped to the framebuffer — so a 16-bpp-only
  card works fine. If the display looks wrong, you can pin a depth by adding a
  line to `AUTOEXEC.BAT` before Quartz, e.g. `SET QUARTZ_BPP=16` (or 24/32).

## Verify in an emulator first
```bash
python3 tools/run_qemu.py --boot "" --disk dist/quartz-usb.img --wait 20
# screenshot -> dist/screen.png
```

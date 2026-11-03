#!/usr/bin/env bash
# mkusb.sh - build a bootable raw USB/HDD image that boots Quartz on real
# hardware (write to a USB stick with:  dd if=dist/quartz-usb.img of=/dev/sdX bs=4M).
#
# A real FreeDOS boot sector (VBR) and MBR bootstrap are installed by FreeDOS
# itself, run headless in QEMU (FDISK /MBR + SYS C:), because there is no
# reliable host-side tool to write a matching FAT16 BPB boot sector.
#
# Usage: tools/mkusb.sh --app build/quartz.exe [--size 256] [--out dist/quartz-usb.img]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"; cd "$ROOT"
export MTOOLS_SKIP_CHECK=1

APP="build/quartz.exe"; SIZE_MB=256; OUT="dist/quartz-usb.img"
while [[ $# -gt 0 ]]; do
  case "$1" in
    --app) APP="$2"; shift 2;;
    --size) SIZE_MB="$2"; shift 2;;
    --out) OUT="$2"; shift 2;;
    *) echo "mkusb: unknown arg $1"; exit 1;;
  esac
done
[[ -f "$APP" ]] || { echo "mkusb: $APP not found (run make)"; exit 1; }

SEED="third_party/freedos/FD13BOOT.img"
CWS="third_party/cwsdpmi/CWSDPMI.EXE"
PART_START=2048
mkdir -p dist build

echo ">> [1/5] create + partition + format $OUT (${SIZE_MB} MB, FAT16)"
dd if=/dev/zero of="$OUT" bs=1M count="$SIZE_MB" status=none
TOTAL_SECTORS=$(( SIZE_MB * 1024 * 1024 / 512 ))
PART_SECTORS=$(( TOTAL_SECTORS - PART_START ))
python3 - "$OUT" "$PART_START" "$PART_SECTORS" <<'PY'
import sys, struct
disk, start, count = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
with open(disk, "r+b") as f:
    f.seek(0x1BE)
    # active (0x80), type 0x0E = FAT16 LBA; CHS left as 0xFE/0xFF (LBA used)
    f.write(struct.pack("<B3sB3sII", 0x80, b"\xfe\xff\xff", 0x0E, b"\xfe\xff\xff",
                        start, count))
    f.seek(0x1FE); f.write(b"\x55\xaa")
PY
mkfs.fat --offset "$PART_START" -F 16 -n QUARTZ "$OUT" >/dev/null
MT="$OUT@@1M"

echo ">> [2/5] build headless FreeDOS install floppy (FDISK /MBR + SYS C:)"
SYSFLOP="dist/_sysfloppy.img"
cp "$SEED" "$SYSFLOP"
printf 'LASTDRIVE=Z\r\nFILES=40\r\nBUFFERS=20\r\nSHELLHIGH=\\FREEDOS\\BIN\\COMMAND.COM \\FREEDOS\\BIN /P=\\FDAUTO.BAT\r\n' > build/_sysflop.cfg
cat > build/_sysflop.bat <<'EOF'
@echo off
SET DOSDIR=\FREEDOS
SET PATH=\FREEDOS\BIN
echo QUARTZ-INSTALL: writing MBR + boot sector to C:
FDISK /MBR
SYS C:
echo QUARTZ-INSTALL: done
FDAPM POWEROFF
EOF
sed -i 's/$/\r/' build/_sysflop.bat
mdel  -i "$SYSFLOP" ::SETUP.BAT 2>/dev/null || true
mcopy -o -i "$SYSFLOP" build/_sysflop.cfg ::FDCONFIG.SYS
mcopy -o -i "$SYSFLOP" build/_sysflop.bat ::FDAUTO.BAT

echo ">> [3/5] run FreeDOS headless to install the boot sector"
python3 tools/run_qemu.py --boot "$SYSFLOP" --disk "$OUT" --wait 14 \
  --out dist/_sysinstall.png >/dev/null 2>&1 || true

echo ">> [4/5] populate the partition with Quartz + runtime"
APPNAME="QUARTZ.EXE"
mcopy -o -i "$MT" "$APP" "::$APPNAME"
mcopy -o -i "$MT" "$CWS" ::CWSDPMI.EXE
[[ -f third_party/freedos/CTMOUSE.EXE ]] && mcopy -o -i "$MT" third_party/freedos/CTMOUSE.EXE ::CTMOUSE.EXE
# bundle packet drivers + Watt-32 config so networking can be enabled on metal
for d in third_party/freedos/pktdrv/*.COM; do
  [[ -f "$d" ]] && mcopy -o -i "$MT" "$d" "::$(basename "$d")"
done
[[ -f third_party/sbemu/SBEMU.EXE ]] && mcopy -o -i "$MT" third_party/sbemu/SBEMU.EXE ::SBEMU.EXE || true
printf 'my_ip = dhcp\r\nhostname = quartz\r\n' > build/_wattcp.cfg
mcopy -o -i "$MT" build/_wattcp.cfg ::WATTCP.CFG

# C: config + autoexec: load mouse, then launch Quartz (no network required)
printf 'LASTDRIVE=Z\r\nFILES=40\r\nBUFFERS=20\r\nSHELL=\\COMMAND.COM /P=\\AUTOEXEC.BAT\r\n' > build/_c_cfg.sys
cat > build/_c_auto.bat <<'EOF'
@echo off
SET DOSDIR=\
SET WATTCP.CFG=C:\
PATH=C:\
if exist C:\CTMOUSE.EXE C:\CTMOUSE.EXE
rem SBEMU brings up SB16 emulation over the HD Audio codec (modern machines).
if exist C:\SBEMU.EXE C:\SBEMU.EXE
rem To enable networking on this machine, load your NIC's packet driver first,
rem e.g. (Realtek 8169):  C:\RTSPKT.COM 0x60   then use the Network app.
C:\QUARTZ.EXE
EOF
sed -i 's/$/\r/' build/_c_auto.bat
mcopy -o -i "$MT" build/_c_cfg.sys  ::FDCONFIG.SYS
mcopy -o -i "$MT" build/_c_auto.bat ::AUTOEXEC.BAT

echo ">> [5/5] contents of the bootable partition:"
mdir -i "$MT" :: | sed 's/^/   /'
rm -f "$SYSFLOP"
echo "OK: $OUT  (dd to a USB stick; boot with CSM/Legacy enabled)"

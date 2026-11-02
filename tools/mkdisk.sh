#!/usr/bin/env bash
# mkdisk.sh - build Quartz dev images:
#   dist/boot.img  : bootable FreeDOS floppy (trimmed), chains to C:\AUTORUN.BAT
#   dist/disk.img  : partitioned FAT16 hard disk with the app + CWSDPMI + mouse
#
# Usage: tools/mkdisk.sh --app build/quartz.exe --run QUARTZ.EXE [extra files...]
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export MTOOLS_SKIP_CHECK=1

APP=""
RUN=""
NET=""          # "" | ne2000 | pcnet | e1000
EXTRA=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --app) APP="$2"; shift 2;;
    --run) RUN="$2"; shift 2;;
    --net) NET="$2"; shift 2;;
    *) EXTRA+=("$1"); shift;;
  esac
done

# packet driver selection (software int 0x60)
PKT_FILE=""; PKT_LOAD=""
case "$NET" in
  ne2000) PKT_FILE="third_party/freedos/pktdrv/NE2000.COM"; PKT_LOAD="NE2000.COM 0x60 3 0x300";;
  pcnet)  PKT_FILE="third_party/freedos/pktdrv/PCNTPK.COM"; PKT_LOAD="PCNTPK.COM 0x60";;
  e1000)  PKT_FILE="third_party/freedos/pktdrv/E1000PKT.COM"; PKT_LOAD="E1000PKT.COM 0x60";;
  "") ;;
  *) echo "mkdisk: unknown --net '$NET'"; exit 1;;
esac
[[ -n "$APP" ]] || { echo "mkdisk: --app required"; exit 1; }
[[ -f "$APP" ]] || { echo "mkdisk: app '$APP' not found"; exit 1; }
APPNAME="$(basename "$APP" | tr '[:lower:]' '[:upper:]')"
[[ -n "$RUN" ]] || RUN="$APPNAME"

mkdir -p dist build
SEED="third_party/freedos/FD13BOOT.img"
CWS="third_party/cwsdpmi/CWSDPMI.EXE"

echo ">> boot floppy"
cp "$SEED" dist/boot.img
# minimal, non-interactive config
printf 'LASTDRIVE=Z\r\nFILES=40\r\nBUFFERS=20\r\nDOS=HIGH,UMB\r\nDEVICE=\\FREEDOS\\BIN\\HIMEMX.EXE\r\nSHELLHIGH=\\FREEDOS\\BIN\\COMMAND.COM \\FREEDOS\\BIN /E:2048 /P=\\FDAUTO.BAT\r\n' > build/FDCONFIG.SYS
# load mouse driver if present on the floppy, then jump to C: and run AUTORUN
cat > build/FDAUTO.BAT <<'EOF'
@echo off
SET DOSDIR=\FREEDOS
SET PATH=\FREEDOS\BIN;C:\;C:\QUARTZ
if exist \CTMOUSE.EXE \CTMOUSE.EXE
C:
if exist \AUTORUN.BAT call \AUTORUN.BAT
EOF
# normalize line endings to CRLF for DOS
sed -i 's/$/\r/' build/FDAUTO.BAT
mdel  -i dist/boot.img ::SETUP.BAT 2>/dev/null || true
mcopy -o -i dist/boot.img build/FDCONFIG.SYS ::FDCONFIG.SYS
mcopy -o -i dist/boot.img build/FDAUTO.BAT   ::FDAUTO.BAT
# vendor ctmouse onto the floppy root so FDAUTO can load it
if [[ -f third_party/freedos/CTMOUSE.EXE ]]; then
  mcopy -o -i dist/boot.img third_party/freedos/CTMOUSE.EXE ::CTMOUSE.EXE
fi

echo ">> data disk (FAT16, partitioned)"
DISK=dist/disk.img
SIZE_MB=48
PART_START=2048                      # sectors (1 MiB)
dd if=/dev/zero of="$DISK" bs=1M count="$SIZE_MB" status=none
# MBR: one FAT16-LBA (0x0E) partition from sector 2048 to end, marked bootable
TOTAL_SECTORS=$(( SIZE_MB * 1024 * 1024 / 512 ))
PART_SECTORS=$(( TOTAL_SECTORS - PART_START ))
python3 - "$DISK" "$PART_START" "$PART_SECTORS" <<'PY'
import sys, struct
disk, start, count = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
with open(disk, "r+b") as f:
    f.seek(0x1BE)
    # status=0x80 (bootable), CHS fields left as 0xFE/0xFF placeholders (LBA used)
    entry = struct.pack("<B3sB3sII", 0x80, b"\xfe\xff\xff", 0x0E, b"\xfe\xff\xff",
                        start, count)
    f.write(entry)
    f.seek(0x1FE)
    f.write(b"\x55\xaa")
PY
# format the partition region in place
mkfs.fat --offset "$PART_START" -F 16 -n QUARTZ "$DISK" >/dev/null

MT="$DISK@@1M"                       # mtools offset to the partition
mcopy -o -i "$MT" "$CWS" ::CWSDPMI.EXE
mcopy -o -i "$MT" "$APP" "::$APPNAME"

# AUTORUN.BAT (optionally load packet driver + Watt-32 DHCP config)
{
  printf '@echo off\r\n'
  if [[ -n "$NET" ]]; then
    printf 'SET WATTCP.CFG=C:\\\r\n'
    printf '%s\r\n' "$PKT_LOAD"
  fi
  printf '%s\r\n' "$RUN"
} > build/AUTORUN.BAT
mcopy -o -i "$MT" build/AUTORUN.BAT ::AUTORUN.BAT

if [[ -n "$NET" ]]; then
  [[ -f "$PKT_FILE" ]] || { echo "mkdisk: packet driver $PKT_FILE missing"; exit 1; }
  mcopy -o -i "$MT" "$PKT_FILE" "::$(basename "$PKT_FILE" | tr '[:lower:]' '[:upper:]')"
  printf 'my_ip = dhcp\r\nhostname = quartz\r\n' > build/WATTCP.CFG
  mcopy -o -i "$MT" build/WATTCP.CFG ::WATTCP.CFG
fi
for f in "${EXTRA[@]:-}"; do
  [[ -n "$f" && -f "$f" ]] && mcopy -o -i "$MT" "$f" "::$(basename "$f" | tr '[:lower:]' '[:upper:]')"
done

echo ">> contents:"
echo "-- A: (boot floppy) --"; mdir -i dist/boot.img :: | sed 's/^/   /'
echo "-- C: (data disk)   --"; mdir -i "$MT" :: | sed 's/^/   /'
echo "OK: dist/boot.img + dist/disk.img  (runs: $RUN)"

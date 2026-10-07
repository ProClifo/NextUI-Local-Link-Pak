#!/bin/sh

# Dedicated NextUI Tool Pak for same-device GBA linking.
# Normal GBA launching remains untouched and continues to use NextUI's GBA.pak/gpSP.

PAK_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
MINARCH="$PAK_DIR/minarch-local-link.elf"
CORE="$PAK_DIR/mgba_local_link_libretro.so"

# Match NextUI's normal h700 userdata/log layout used by the other paks.
SD_ROOT="${SDCARD_PATH:-/mnt/sdcard}"
USERDATA_DIR="${USERDATA_PATH:-$SD_ROOT/.userdata/h700}"
LOG_DIR="$USERDATA_DIR/logs"

mkdir -p "$LOG_DIR" "$USERDATA_DIR"

if [ ! -x "$MINARCH" ]; then
    echo "Local Link: missing executable $MINARCH" >> "$LOG_DIR/Local-Link.txt"
    exit 1
fi
if [ ! -f "$CORE" ]; then
    echo "Local Link: missing core $CORE" >> "$LOG_DIR/Local-Link.txt"
    exit 1
fi

# Local Link deliberately uses the mGBA-tagged ROM folder. Normal GBA.pak/gpSP
# continues to use Game Boy Advance (GBA).
export LOCAL_LINK_ROM_DIR="${LOCAL_LINK_ROM_DIR:-$SD_ROOT/Roms/Game Boy Advance (MGBA)}"
HOME="$USERDATA_DIR"
export HOME

{
    echo "=== NextUI Local Link ==="
    echo "pak=$PAK_DIR"
    echo "rom_dir=$LOCAL_LINK_ROM_DIR"
    echo "userdata=$HOME"
    echo "device=${DEVICE:-unknown}"
    echo
} > "$LOG_DIR/Local-Link.txt"

cd "$HOME" || exit 1
"$MINARCH" "$CORE" >> "$LOG_DIR/Local-Link.txt" 2>&1
status=$?
echo >> "$LOG_DIR/Local-Link.txt"
echo "exit_status=$status" >> "$LOG_DIR/Local-Link.txt"
exit "$status"

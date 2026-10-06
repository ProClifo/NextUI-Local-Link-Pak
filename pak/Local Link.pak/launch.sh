#!/bin/sh

# Dedicated NextUI Tool Pak for same-device GBA linking.
# Normal GBA launching remains untouched and continues to use NextUI's GBA.pak/gpSP.

PAK_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
MINARCH="$PAK_DIR/minarch-local-link.elf"
CORE="$PAK_DIR/mgba_local_link_libretro.so"

if [ ! -x "$MINARCH" ]; then
    echo "Local Link: missing executable $MINARCH" >&2
    exit 1
fi
if [ ! -f "$CORE" ]; then
    echo "Local Link: missing core $CORE" >&2
    exit 1
fi

# The patched MinArch treats an omitted ROM argument as Tool mode and opens
# its first-ROM picker. LOCAL_LINK_ROM_DIR can override the default path.
export LOCAL_LINK_ROM_DIR="${LOCAL_LINK_ROM_DIR:-/mnt/SDCARD/Roms/Game Boy Advance (GBA)}"
HOME="$USERDATA_PATH"
export HOME
cd "$HOME" || exit 1

"$MINARCH" "$CORE" > "$LOGS_PATH/Local-Link.txt" 2>&1

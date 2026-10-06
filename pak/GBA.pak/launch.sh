#!/bin/sh

# Local Link replacement for NextUI's normal GBA.pak.
#
# This Pak deliberately keeps the GBA tag so existing ROMs in
# Roms/Game Boy Advance (GBA) continue to launch normally. It bundles a patched
# MinArch and a Local-Link-enabled mGBA core; no files under .system are replaced.

PAK_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
EMU_TAG="GBA"
ROM="$1"

if [ -z "$ROM" ]; then
    echo "Local Link GBA: no ROM supplied" >&2
    exit 1
fi

MINARCH="$PAK_DIR/minarch-local-link.elf"
CORE="$PAK_DIR/mgba_local_link_libretro.so"

if [ ! -x "$MINARCH" ]; then
    echo "Local Link GBA: missing executable $MINARCH" >&2
    exit 1
fi
if [ ! -f "$CORE" ]; then
    echo "Local Link GBA: missing core $CORE" >&2
    exit 1
fi

mkdir -p "$BIOS_PATH/$EMU_TAG" "$SAVES_PATH/$EMU_TAG" "$CHEATS_PATH/$EMU_TAG"
HOME="$USERDATA_PATH"
export HOME
cd "$HOME" || exit 1

# h700's default shell is not reliable with '&>' redirection.
"$MINARCH" "$CORE" "$ROM" > "$LOGS_PATH/$EMU_TAG-local-link.txt" 2>&1

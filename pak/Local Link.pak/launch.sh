#!/bin/sh

# NextUI Local Link Pak launcher.
# The production build places its Local-Link-enabled mGBA core next to this
# script and launches it through MinArch so the normal NextUI menu remains in
# charge of Add Instance / Save / Load / Quit behavior.

PAK_DIR="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
ROM="$1"
EMU_TAG="LOCAL-LINK"

if [ -z "$ROM" ]; then
    echo "Local Link: no ROM supplied" >&2
    exit 1
fi

CORE="$PAK_DIR/mgba_local_link_libretro.so"
if [ ! -f "$CORE" ]; then
    echo "Local Link: missing $CORE" >&2
    exit 1
fi

mkdir -p "$BIOS_PATH/$EMU_TAG" "$SAVES_PATH/$EMU_TAG" "$CHEATS_PATH/$EMU_TAG"
HOME="$USERDATA_PATH"
export HOME
cd "$HOME" || exit 1

# H700's shell does not reliably support '&>' redirection.
minarch.elf "$CORE" "$ROM" > "$LOGS_PATH/$EMU_TAG.txt" 2>&1

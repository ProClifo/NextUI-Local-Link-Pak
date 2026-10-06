#!/bin/sh
set -eu

PROJECT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BUILD_DIR=${BUILD_DIR:-"$PROJECT_DIR/build/h700"}
DIST_DIR=${DIST_DIR:-"$PROJECT_DIR/dist"}
TOOLCHAIN_IMAGE=${TOOLCHAIN_IMAGE:-ghcr.io/loveretro/h700-toolchain:latest}
NEXTUI_REPO=${NEXTUI_REPO:-https://github.com/LoveRetro/NextUI.git}
MGBA_REPO=${MGBA_REPO:-https://github.com/libretro/mgba.git}
MGBA_REV=${MGBA_REV:-925f0f0bc1f51c79ca5446d7427512c653e615b3}

NEXTUI_DIR="$BUILD_DIR/NextUI"
MGBA_DIR="$BUILD_DIR/mgba"
OUTPUT="$DIST_DIR/NextUI-Local-Link-h700.zip"

command -v docker >/dev/null 2>&1 || {
    echo "error: Docker is required to build h700 binaries" >&2
    exit 1
}

mkdir -p "$BUILD_DIR" "$DIST_DIR"

if [ ! -d "$NEXTUI_DIR/.git" ]; then
    rm -rf "$NEXTUI_DIR"
    git clone --depth 1 "$NEXTUI_REPO" "$NEXTUI_DIR"
else
    git -C "$NEXTUI_DIR" fetch --depth 1 origin main
    git -C "$NEXTUI_DIR" reset --hard origin/main
    git -C "$NEXTUI_DIR" clean -fdx
fi

if [ ! -d "$MGBA_DIR/.git" ]; then
    rm -rf "$MGBA_DIR"
    git clone "$MGBA_REPO" "$MGBA_DIR"
else
    git -C "$MGBA_DIR" fetch origin "$MGBA_REV"
    git -C "$MGBA_DIR" reset --hard
    git -C "$MGBA_DIR" clean -fdx
fi
git -C "$MGBA_DIR" checkout --detach "$MGBA_REV"

python3 "$PROJECT_DIR/scripts/apply_nextui.py" "$NEXTUI_DIR"
python3 "$PROJECT_DIR/scripts/integrate_tool_mode.py" "$NEXTUI_DIR"
python3 "$PROJECT_DIR/scripts/prepare_mgba.py" "$MGBA_DIR"
printf 'local-link-test\n' > "$NEXTUI_DIR/workspace/hash.txt"

echo "==> Pulling h700 toolchain"
docker pull "$TOOLCHAIN_IMAGE"

echo "==> Building picker-enabled Local-Link MinArch"
docker run --rm \
    -v "$NEXTUI_DIR:/src/NextUI" \
    -w /src/NextUI/workspace/all/minarch \
    "$TOOLCHAIN_IMAGE" \
    sh -lc 'make PLATFORM=h700 -j1'

MINARCH="$NEXTUI_DIR/workspace/all/minarch/build/h700/minarch.elf"
[ -f "$MINARCH" ] || {
    echo "error: MinArch build did not produce $MINARCH" >&2
    exit 1
}

echo "==> Building Local-Link mGBA core"
docker run --rm \
    -v "$MGBA_DIR:/src/mgba" \
    -w /src/mgba \
    "$TOOLCHAIN_IMAGE" \
    sh -lc 'make -f Makefile.libretro NEXTUI_LOCAL_LINK=1 platform=tg5040 -j"$(nproc)"'

CORE="$MGBA_DIR/mgba_libretro.so"
[ -f "$CORE" ] || {
    echo "error: mGBA build did not produce $CORE" >&2
    exit 1
}

if command -v readelf >/dev/null 2>&1; then
    readelf -Ws "$CORE" | grep -q 'retro_local_link_get_abi_version' || {
        echo "error: built mGBA core does not export Local Link ABI" >&2
        exit 1
    }
fi

echo "==> Packaging dedicated Tool Pak: $OUTPUT"
rm -f "$OUTPUT"
python3 "$PROJECT_DIR/scripts/package_pak.py" \
    --platform h700 \
    --minarch "$MINARCH" \
    --core "$CORE" \
    --output "$OUTPUT"

echo
echo "Built: $OUTPUT"
echo "Install by extracting at the NextUI SD-card root."
echo "This installs Tools/h700/Local Link.pak and leaves Emus/h700/GBA.pak untouched."

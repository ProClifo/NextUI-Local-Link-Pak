#!/usr/bin/env python3
"""Prepare the pinned libretro/mGBA source tree for NextUI Local Link.

The NextUI-era mGBA revision uses Makefile.libretro + libretro-build/Makefile.common.
This script adds the GBA/GB lockstep sources and selects our multi-instance frontend when
NEXTUI_LOCAL_LINK=1 is passed to Makefile.libretro.

Usage:
    python3 scripts/prepare_mgba.py /path/to/libretro-mgba
"""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


class PrepareError(RuntimeError):
    pass


def replace_once(path: Path, old: str, new: str, marker: str) -> None:
    text = path.read_text()
    if marker in text:
        return
    if old not in text:
        raise PrepareError(f"anchor not found in {path}: {old[:120]!r}")
    path.write_text(text.replace(old, new, 1))


def copy_if_changed(src: Path, dst: Path) -> None:
    data = src.read_bytes()
    if dst.exists() and dst.read_bytes() == data:
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    dst.write_bytes(data)


def prepare(mgba: Path, project: Path) -> None:
    common = mgba / "libretro-build/Makefile.common"
    libretro_make = mgba / "Makefile.libretro"
    frontend_dir = mgba / "src/platform/libretro"
    if not common.exists() or not libretro_make.exists() or not frontend_dir.exists():
        raise PrepareError(
            "mGBA checkout does not have the pinned libretro build layout; "
            "use libretro/mgba revision 925f0f0bc1f51c79ca5446d7427512c653e615b3"
        )

    copy_if_changed(project / "cores/mgba/libretro_local_link.c",
                    frontend_dir / "libretro_local_link.c")
    copy_if_changed(project / "include/local_link_abi.h",
                    frontend_dir / "local_link_abi.h")

    # Lockstep driver objects are not part of the ordinary libretro core build.
    replace_once(
        common,
        "\t\t\t\t\t$(CORE_DIR)/src/gb/sio.c \\\n",
        "\t\t\t\t\t$(CORE_DIR)/src/gb/sio.c \\\n"
        "\t\t\t\t\t$(CORE_DIR)/src/gb/sio/lockstep.c \\\n",
        "src/gb/sio/lockstep.c",
    )
    replace_once(
        common,
        "\t\t\t\t\t$(CORE_DIR)/src/gba/sio/gbp.c \\\n",
        "\t\t\t\t\t$(CORE_DIR)/src/gba/sio/gbp.c \\\n"
        "\t\t\t\t\t$(CORE_DIR)/src/gba/sio/lockstep.c \\\n",
        "src/gba/sio/lockstep.c",
    )
    replace_once(
        common,
        "\t\t\t\t\t$(CORE_DIR)/src/platform/libretro/libretro.c \\\n",
        "\t\t\t\t\t$(if $(filter 1,$(NEXTUI_LOCAL_LINK)),$(CORE_DIR)/src/platform/libretro/libretro_local_link.c,$(CORE_DIR)/src/platform/libretro/libretro.c) \\\n",
        "NEXTUI_LOCAL_LINK",
    )

    # NextUI's h700/aarch64 toolchain can use the same Cortex-A53 branch that
    # bmpriest proved with the dual-instance core. Keep the platform addition
    # local to this prepared source tree.
    platform_anchor = "# Windows\nelse\n"
    platform_block = """# NextUI aarch64 (tg5040/h700 build profile)\nelse ifeq ($(platform), tg5040)\n   TARGET := $(TARGET_NAME)_libretro.so\n   CC = $(CROSS_COMPILE)gcc\n   CXX = $(CROSS_COMPILE)g++\n   AR = $(CROSS_COMPILE)ar\n   SHARED := -shared -Wl,--version-script=link.T\n   fpic := -fPIC\n   PLATFORM_DEFINES += -D_GNU_SOURCE -DHAVE_STRTOF_L -DHAVE_LOCALE\n   CFLAGS += -fomit-frame-pointer -ffast-math\n   CFLAGS += -mtune=cortex-a53 -mcpu=cortex-a53 -march=armv8-a\n   CFLAGS += -fno-common -ftree-vectorize -funswitch-loops\n   HAVE_NEON = 1\n   ARCH = arm64\n   BUILTIN_GPU = neon\n   CPU_ARCH := arm\n   MMAP_JIT_CACHE = 1\n   HAVE_DYNAREC = 1\n   DEFINES += -std=c99\n\n"""
    replace_once(libretro_make, platform_anchor, platform_block + platform_anchor,
                 "NextUI aarch64 (tg5040/h700 build profile)")

    print(f"Local Link mGBA source prepared at {mgba}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("mgba", type=Path)
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    try:
        prepare(args.mgba.resolve(), project)
    except PrepareError as exc:
        print(f"ERROR: {exc}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

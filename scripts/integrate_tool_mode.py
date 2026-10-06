#!/usr/bin/env python3
"""Add Local Link Tool-mode startup to an already Local-Link-patched NextUI checkout."""

from __future__ import annotations

import argparse
import shutil
from pathlib import Path


class ToolModeError(RuntimeError):
    pass


def replace_once(path: Path, old: str, new: str, marker: str) -> None:
    text = path.read_text()
    if marker in text:
        return
    if old not in text:
        raise ToolModeError(f"anchor not found in {path}: {old[:100]!r}")
    path.write_text(text.replace(old, new, 1))


def copy_if_changed(src: Path, dst: Path) -> None:
    data = src.read_bytes()
    if dst.exists() and dst.read_bytes() == data:
        return
    dst.write_bytes(data)


def integrate(nextui: Path, project: Path) -> None:
    ma = nextui / "workspace/all/minarch"
    if not (ma / "minarch.c").is_file():
        raise ToolModeError(f"{nextui} does not look like a NextUI checkout")

    copy_if_changed(project / "integration/nextui/local_link_picker.c", ma / "local_link_picker.c")
    copy_if_changed(project / "integration/nextui/local_link_picker.h", ma / "local_link_picker.h")

    makefile = ma / "makefile"
    replace_once(
        makefile,
        "ma_runframe.c minarch_local_link.c \\\n",
        "ma_runframe.c minarch_local_link.c local_link_picker.c \\\n",
        marker="minarch_local_link.c local_link_picker.c",
    )

    main = ma / "minarch.c"
    replace_once(
        main,
        '#include "ma_runframe.h"\n',
        '#include "ma_runframe.h"\n#include "local_link_picker.h"\n',
        marker='#include "local_link_picker.h"',
    )

    old_args = '''\tstrcpy(core_path, argv[1]);
\tstrcpy(rom_path, argv[2]);
\tgetEmuName(rom_path, tag_name);
\t
\tLOG_info("rom_path: %s\\n", rom_path);
\t
\tscreen = GFX_init(MODE_MENU);
'''
    new_args = '''\tstrcpy(core_path, argv[1]);
\trom_path[0] = '\\0';
\tif (argc >= 3 && argv[2] && argv[2][0]) {
\t\tsnprintf(rom_path, sizeof(rom_path), "%s", argv[2]);
\t}
\n\tscreen = GFX_init(MODE_MENU);
'''
    replace_once(main, old_args, new_args, marker="rom_path[0] = '\\0';")

    old_open = '''\tMSG_init();
\tIMG_Init(IMG_INIT_PNG);
\tCore_open(core_path, tag_name);

\tGame_open(rom_path); // nes tries to load gamegenie setting before this returns ffs
'''
    new_open = '''\tMSG_init();
\tIMG_Init(IMG_INIT_PNG);

\t/* Tools/<platform>/Local Link.pak launches MinArch with only the core.
\t * In that mode, choose Player 1 before opening the core/game. Normal
\t * emulator launches still pass argv[2] and bypass this picker entirely. */
\tif (!rom_path[0]) {
\t\tconst char *rom_dir = getenv("LOCAL_LINK_ROM_DIR");
\t\tif (!rom_dir || !rom_dir[0]) rom_dir = ROMS_PATH "/Game Boy Advance (GBA)";
\t\tif (!LLPicker_pickFirstRom(rom_dir, rom_path, sizeof(rom_path))) {
\t\t\tIMG_Quit();
\t\t\tMSG_quit();
\t\t\tPWR_quit();
\t\t\tVIB_quit();
\t\t\tPAD_quit();
\t\t\tGFX_quit();
\t\t\treturn EXIT_SUCCESS;
\t\t}
\t}
\n\tgetEmuName(rom_path, tag_name);
\tLOG_info("rom_path: %s\\n", rom_path);
\tCore_open(core_path, tag_name);

\tGame_open(rom_path); // nes tries to load gamegenie setting before this returns ffs
'''
    replace_once(main, old_open, new_open, marker="LLPicker_pickFirstRom")

    print(f"Local Link Tool mode applied to {nextui}")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("nextui", type=Path)
    args = parser.parse_args()
    project = Path(__file__).resolve().parents[1]
    try:
        integrate(args.nextui.resolve(), project)
    except ToolModeError as exc:
        print(f"ERROR: {exc}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

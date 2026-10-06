#!/usr/bin/env python3
"""Build an SD-card-ready NextUI Local Link Tool Pak archive.

The archive installs only under `Tools/<platform>/Local Link.pak`. It does not replace
NextUI's normal `Emus/<platform>/GBA.pak`, so ordinary GBA launching remains on gpSP.

Usage:
    python3 scripts/package_pak.py \
        --platform h700 \
        --minarch /path/to/minarch.elf \
        --core /path/to/mgba_libretro.so \
        --output dist/Local-Link-h700.zip
"""

from __future__ import annotations

import argparse
import shutil
import stat
import tempfile
import zipfile
from pathlib import Path


def copy_executable(src: Path, dst: Path) -> None:
    if not src.is_file():
        raise SystemExit(f"missing file: {src}")
    shutil.copy2(src, dst)
    dst.chmod(dst.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform", required=True)
    parser.add_argument("--minarch", required=True, type=Path)
    parser.add_argument("--core", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    project = Path(__file__).resolve().parents[1]
    launcher = project / "pak/Local Link.pak/launch.sh"
    if not launcher.is_file():
        raise SystemExit(f"missing launcher: {launcher}")
    if not args.core.is_file():
        raise SystemExit(f"missing file: {args.core}")

    args.output.parent.mkdir(parents=True, exist_ok=True)

    with tempfile.TemporaryDirectory(prefix="local-link-pak-") as tmp:
        root = Path(tmp)
        pak = root / "Tools" / args.platform / "Local Link.pak"
        pak.mkdir(parents=True)

        copy_executable(launcher, pak / "launch.sh")
        copy_executable(args.minarch, pak / "minarch-local-link.elf")
        shutil.copy2(args.core, pak / "mgba_local_link_libretro.so")

        (pak / "LOCAL_LINK.txt").write_text(
            "NextUI Local Link Tool Pak\n"
            f"platform={args.platform}\n"
            "Install by extracting at the SD-card root.\n"
            "Normal Emus/<platform>/GBA.pak is not modified.\n"
            "Launch Local Link from Tools and choose Player 1.\n"
        )

        with zipfile.ZipFile(args.output, "w", zipfile.ZIP_DEFLATED) as archive:
            for path in sorted(root.rglob("*")):
                if path.is_file():
                    archive.write(path, path.relative_to(root).as_posix())

    print(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

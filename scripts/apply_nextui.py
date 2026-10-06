#!/usr/bin/env python3
"""Stable entry point for applying Local Link to a NextUI checkout.

Small, semantics-free formatting normalizations live here so integrate_nextui.py can
continue to use strict structural anchors. This prevents harmless whitespace changes in
upstream NextUI from looking like API changes while still failing on real source drift.
"""

from __future__ import annotations

import argparse
from pathlib import Path

from integrate_nextui import IntegrationError, integrate


def normalize_upstream(nextui: Path) -> None:
    saves_h = nextui / "workspace/all/minarch/ma_saves.h"
    if saves_h.exists():
        text = saves_h.read_text()
        text = text.replace("int  State_read(void);", "int State_read(void);")
        text = text.replace("int  State_write(void);", "int State_write(void);")
        saves_h.write_text(text)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("nextui", type=Path)
    args = parser.parse_args()

    project = Path(__file__).resolve().parents[1]
    nextui = args.nextui.resolve()
    normalize_upstream(nextui)
    try:
        integrate(nextui, project)
    except IntegrationError as exc:
        print(f"ERROR: {exc}")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

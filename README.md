# NextUI Local Link Pak

Local multi-instance Game Boy Advance linking for NextUI.

## Goal

Run **2–4 linked GBA games on one NextUI device** for uses such as:

- Pokémon trading and battling
- event/distribution ROMs
- Pokémon + e-Reader workflows
- normal 2–4 player GBA link games

## Intended UX

The feature lives inside the normal NextUI emulator menu:

```text
Continue
Save
Load
Options
  Emulator
    Add Instance >
    Switch Instance
    Remove Instance
  Controls
Quit
```

`Add Instance` uses the same start/resume idea as normal NextUI:

- **A — Start** from battery save (`.sav`/`.srm`)
- **X — Resume** the game's auto save state

Player/link slots are fixed for the lifetime of a session. If Player 2 disconnects, Player 3 remains Player 3.

When more than one instance is running:

- **Quit** saves the active game's battery RAM and disconnects only that GBA.
- **Save & Quit** saves the active game's battery RAM + auto-resume state and disconnects only that GBA.
- The remaining instances continue running and retain their original player numbers.

When one instance remains, NextUI returns to its normal Quit / Save & Quit behavior.

## Architecture

The custom mGBA libretro frontend owns up to four `mCore` instances and connects occupied slots through mGBA's in-process `GBASIOLockstepCoordinator`.

A small optional extension ABI lets MinArch:

- add/remove GBA instances
- choose the visible/controlled instance
- access SRAM per player
- serialize/unserialize states per player
- preserve fixed P1–P4 cable IDs

The design is informed by the dual-instance mGBA work in `bmpriest/nextui-netplay` and the 1–4 player lockstep implementation in `Spuds0588/mgba-splitscreen`.

## Repository layout

- `cores/mgba/` — 1–4 instance mGBA libretro frontend
- `include/local_link_abi.h` — optional MinArch/core extension ABI
- `integration/nextui/` — MinArch integration helpers
- `patches/nextui/` — patches against upstream NextUI
- `src/` — standalone/session prototypes and backend work
- `tests/` — host-side behavior tests

## Status

Work in progress. The session model, fixed player slots, 1–4 mGBA lockstep frontend, MinArch `Add Instance` integration, per-player SRAM/state handling, and per-instance Quit semantics are implemented in source/patch form. Device builds and real-hardware testing are still required.

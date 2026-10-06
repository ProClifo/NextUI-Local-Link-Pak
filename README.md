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

`Add Instance` follows NextUI's normal start/resume idea:

- **A — Start** from battery save (`.sav`/`.srm`)
- **X — Resume** the game's auto save state

Local Link slots are fixed for the lifetime of a session. If the UI has P1, P2 and P3 and P2 is removed, the remaining ROM/save/state ownership stays P1 and P3 rather than being renumbered.

When more than one instance is running:

- **Quit** saves the active game's battery RAM and disconnects only that GBA.
- **Save & Quit** saves the active game's battery RAM + auto-resume state and disconnects only that GBA.
- The remaining instances continue running.

When one instance remains, NextUI returns to its normal Quit / Save & Quit behavior.

## Important cable-ID note

The current implementation preserves fixed **Local Link slots** P1–P4. Stock mGBA's lockstep coordinator, however, compacts its internal multiplayer player IDs after a participant disconnects. Therefore sticky in-game GBA cable IDs across a gap (for example, ensuring the original physical P3 remains cable ID 3 after P2 powers off) are **not implemented yet**. A separate capability is reserved for this and the project will not claim it until the underlying lockstep behavior is patched and tested.

This distinction does not block the basic two-instance Pokémon trading/event-distribution milestone, but it matters for authentic 3–4 player disconnect behavior.

## Architecture

The custom mGBA libretro frontend owns up to four `mCore` instances and connects occupied slots through mGBA's in-process `GBASIOLockstepCoordinator`.

A small optional extension ABI lets the bundled MinArch:

- add/remove GBA instances
- choose the visible/controlled instance
- access SRAM per slot
- serialize/unserialize states per slot
- preserve fixed P1–P4 Local Link slot ownership

The design is informed by the dual-instance mGBA work in `bmpriest/nextui-netplay` and the 1–4 player lockstep implementation in `Spuds0588/mgba-splitscreen`.

## h700 installation model

For h700 devices (including RG35XXSP), the finished build is a drop-in override at:

```text
Emus/h700/GBA.pak/
  launch.sh
  minarch-local-link.elf
  mgba_local_link_libretro.so
```

It deliberately keeps the normal `GBA` tag, so ROMs stay in:

```text
Roms/Game Boy Advance (GBA)/
```

The Pak bundles its own patched MinArch instead of replacing files in NextUI's hidden `.system` directory. Removing `Emus/h700/GBA.pak` returns the device to NextUI's built-in GBA Pak.

## Building h700

With Docker installed:

```sh
./scripts/build_h700.sh
```

This clones current NextUI plus the pinned libretro/mGBA revision, applies the Local Link integrations, builds both binaries with NextUI's official `ghcr.io/loveretro/h700-toolchain` image, and creates:

```text
dist/NextUI-Local-Link-h700.zip
```

Extract that ZIP at the root of the NextUI SD card.

## Repository layout

- `cores/mgba/` — 1–4 instance mGBA libretro frontend
- `include/local_link_abi.h` — optional MinArch/core extension ABI
- `integration/nextui/` — MinArch integration helpers
- `scripts/apply_nextui.py` — idempotent NextUI integration entry point
- `scripts/prepare_mgba.py` — prepares pinned mGBA for Local Link
- `scripts/build_h700.sh` — cross-builds and packages h700
- `scripts/package_pak.py` — creates the SD-card-ready GBA.pak ZIP
- `pak/GBA.pak/` — drop-in GBA Pak launcher
- `src/` — session/backend prototypes
- `tests/` — host-side behavior tests

## Status

Work in progress. Host behavior tests, current-NextUI integration, the custom Local-Link mGBA core build, and Pak layout are covered by CI. Real h700 hardware testing is still required, and sticky in-game cable IDs for 3–4 player disconnect scenarios remain an open implementation task.

# NextUI / MinArch integration

Local Link is exposed as an **optional libretro extension ABI**. This is deliberate: MinArch already owns one loaded libretro core, so the custom mGBA core must own P1-P4 internally rather than MinArch loading a second, unrelated copy of libmgba.

## Flow

1. MinArch loads the Local Link mGBA core normally.
2. `Core_open()` discovers `retro_local_link_*` symbols with `dlsym()`.
3. P1 is the game launched by normal `retro_load_game()`.
4. `Options > Emulator > Add Instance` scans the current GBA ROM folder and adds the selected ROM to the first free fixed slot (P2, then P3/P4).
5. The core keeps every loaded mGBA instance advancing in lockstep.
6. `Switch Instance` cycles only the visible/controlled player; it does not alter cable IDs.
7. `Remove Instance` disconnects the active slot without renumbering survivors.

## Patches

- `001-local-link-core-abi.patch` adds optional Local Link function pointers to MinArch's `Core` and discovers them with `dlsym()`.
- `002-local-link-emulator-menu.patch` injects `Add Instance`, `Switch Instance`, and `Remove Instance` at the root of `Options > Emulator` only when the loaded core advertises Local Link support.

The picker intentionally starts simple: it lists `.gba` files beside the currently running ROM, rendered with NextUI's existing `Menu_options()` UI. Recursive folders and the normal A/X start-vs-resume behavior come after the two-instance proof is working on h700.

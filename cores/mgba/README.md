# mGBA Local Link core

`libretro_local_link.c` is the custom libretro frontend for the project.

The core owns P1-P4 itself. P1 is loaded through standard `retro_load_game`; P2-P4 are loaded using the optional `retro_local_link_add_instance` ABI discovered by patched MinArch.

Current behavior:

- Up to four fixed slots.
- One `GBASIOLockstepCoordinator` when two or more slots are occupied.
- Slot number is requested as the physical GBA cable ID, so gaps are preserved.
- All loaded consoles advance each frame.
- The handheld controls only the active console.
- Only the active console supplies video and audio.
- Per-player save RAM and save-state APIs are exported for MinArch integration.

Still to validate/fix during the h700 build:

- Exact mGBA revision/API compatibility used by current NextUI.
- Safe SIO driver detach/rebuild behavior while games are already linked.
- BIOS lookup/config propagation from the stock NextUI mGBA core.
- Performance with three/four simultaneous GBA instances.

# NextUI / MinArch integration

Local Link is exposed as an **optional libretro extension ABI**. This is deliberate: MinArch already owns one loaded libretro core, so the custom mGBA core must own P1-P4 internally rather than MinArch loading a second, unrelated copy of libmgba.

## Flow

1. MinArch loads the Local Link mGBA core normally.
2. `Core_open()` discovers `retro_local_link_*` symbols with `dlsym()`.
3. P1 is the game launched by normal `retro_load_game()`.
4. `Options > Emulator > Add Instance` asks MinArch's Local Link bridge to add P2, then P3/P4.
5. The core keeps every loaded mGBA instance advancing in lockstep.
6. Only the active slot supplies video/audio and receives local controls in the first UI version.

`patches/nextui/001-local-link-core-abi.patch` adds the optional function pointers to current NextUI MinArch. The menu/picker patch is kept separate so the ABI can stabilize independently.

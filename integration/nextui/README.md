# NextUI / MinArch integration

Local Link is exposed as an **optional libretro extension ABI**. MinArch still loads one libretro core; the Local-Link-enabled mGBA core owns P1-P4 internally and exposes a small set of optional `retro_local_link_*` entry points.

## User flow

1. NextUI launches P1 normally.
2. `Core_open()` discovers the optional Local Link symbols with `dlsym()`.
3. `Options > Emulator > Add Instance` lists `.gba` files beside the currently running ROM.
4. After choosing a ROM:
   - **A — START** loads its battery save.
   - **X — RESUME** restores its auto-resume state.
5. New games occupy the first free fixed link slot (P2, P3, then P4).
6. `Switch Instance` changes only which GBA is displayed and controlled.
7. `Remove Instance` saves battery RAM and disconnects only the active GBA.
8. With multiple instances, normal **Quit** disconnects only the active GBA; **Save & Quit** saves that GBA's battery RAM + auto state and then disconnects it.
9. Remaining slots keep their original player numbers.

## Integrating into NextUI

Use the repository's idempotent entry point against a clean NextUI checkout:

```sh
python3 scripts/apply_nextui.py /path/to/NextUI
```

`apply_nextui.py` tolerates harmless upstream formatting differences, then delegates the structural work to the strict `integrate_nextui.py` implementation. The integration:

- copies `minarch_local_link.c/.h` and `local_link_abi.h` into MinArch,
- adds the helper to MinArch's build,
- adds optional ABI pointers/discovery to `Core`,
- adds per-ROM SRAM and state helpers,
- injects Local Link actions into `Options > Emulator`,
- changes Quit / Save & Quit to per-instance behavior while peers remain.

The structural integration uses exact upstream source anchors and intentionally fails if an expected API/code shape changes. CI runs the entry point twice against current `LoveRetro/NextUI` main to check compatibility and idempotency.

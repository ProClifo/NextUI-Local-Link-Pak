# Architecture

## Goal

Run up to four linked GBA instances on one NextUI device while making the feature feel like part of the normal GBA emulator.

## Fixed physical player slots

The session owns four permanent slots: P1, P2, P3 and P4. A slot maps directly to the GBA link-cable player ID for the life of the session.

Example:

```text
P1  Pokemon Emerald
P2  Pokemon FireRed
P3  e-Reader
P4  empty
```

If P2 quits, the topology becomes P1 + P3. P3 does **not** become P2. The native backend implements this by returning the slot index from mGBA's `mLockstepUser.requestedId` callback.

## Layers

```text
NextUI menu / hotkeys
        |
        v
LLSession
  - fixed P1-P4 slots
  - active/visible player
  - Quit / Save & Quit semantics
        |
        v
LLBackend
        |
        +-- host stub (tests)
        |
        +-- native libmgba backend
              - 1 mCore per occupied slot
              - GBASIOLockstepDriver per linked instance
              - one GBASIOLockstepCoordinator
              - round-robin frame stepping
```

## Active instance

All loaded cores continue to execute. Only one is considered active for display, audio and physical controls in the first UI version.

The intended hotkey cycles among occupied slots without altering link IDs.

## Quit behavior

With multiple instances loaded:

- Quit closes only the active instance.
- Save & Quit saves and closes only the active instance.
- Remaining instances continue running and keep their original player IDs.
- The UI selects another occupied slot as active.

With one instance left, Quit/Save & Quit can fall back to the normal NextUI behavior.

## Adding/removing instances

Whenever occupancy changes, the current prototype rebuilds the mGBA lockstep coordinator and reattaches every loaded instance using its fixed requested ID. This keeps the UI/session model simple while we validate mGBA's behavior with gaps such as P1 + P3.

If testing shows that rebuilding the coordinator disturbs games already in a link state, the next implementation will switch to in-place `GBASIOLockstepCoordinatorAttach/Detach` while keeping the same fixed-ID model.

## Immediate milestones

1. Cross-build the native backend for h700.
2. Load two ROMs with their real NextUI battery-save paths.
3. Route controls only to the active instance.
4. Switch the displayed framebuffer between P1/P2.
5. Complete and persist a Pokemon trade.
6. Add `Options > Emulator > Add Instance`.
7. Extend validation to P1-P4 and e-Reader/event workflows.

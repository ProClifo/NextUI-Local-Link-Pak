# Third-party references

This project builds on APIs and implementation ideas from existing open-source emulator projects.

## mGBA

mGBA is licensed under the Mozilla Public License 2.0. The Local Link backend uses mGBA's public/internal core and GBA SIO lockstep APIs.

Upstream: https://github.com/mgba-emu/mgba

## bmpriest/nextui-netplay

The initial in-process multi-instance design was informed by the `mGBA Dual` frontend in `bmpriest/nextui-netplay`, which demonstrated two mGBA cores attached to one `GBASIOLockstepCoordinator` under NextUI. That repository is MIT licensed.

Reference: https://github.com/bmpriest/nextui-netplay

## mgba-splitscreen

`Spuds0588/mgba-splitscreen` is used as a reference for 1-4 local mGBA instances, lockstep frame scheduling, and multi-instance UX.

Reference: https://github.com/Spuds0588/mgba-splitscreen

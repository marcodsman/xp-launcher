# OpenBOR — modern engine (SDL2, build 7533) — the standard for all OpenBOR games

**Key discovery (2026-07-10):** the *modern* SDL2 OpenBOR runs fine on the XP box. It tries
OpenGL, fails on the GMA500, and **auto-falls-back to SDL's Direct3D9 renderer** (the box's
fast path). This unlocked the "OpenBOR 4.0" games that were wrongly assumed unrunnable. It also
fullscreens via SDL2 fullscreen-desktop (no display-mode switch), which avoids the
resolution-break-on-exit that the old SDL 1.2 engine caused.

## Layout
Each OpenBOR game is its own folder on the box: `C:\XP_Share\openbor-<slug>\` =
`OpenBOR.exe` (this build 7533) + `Paks\<GAME>.pak` + `Saves\<GAME>.cfg`. Paks are game data
(freeware fan paks, NOT repo-tracked, like BOR.PAK). Beats of Rage keeps the plain `openbor\`
folder. All share this one engine + one config template.

## Config (`default.cfg` here = the patched template)
`Saves\<PAKBASE>.cfg` is a 324-byte `s_savedata` (OpenBOR v7533). We pre-place a patched copy
(named for each pak) so fullscreen + pad work from first launch — no in-game setup. Exact
offsets + the pad mapping are in `scripts/patch-openbor-config.py` and memory
`openbor-borcfg-format`: `usejoy`@0x1c, `keys[4][13]`@0x28, `fullscreen`@0x120, `stretch`@0x124.
Pad: P1 = joystick port 1, P2 = port 0 (Twin USB adapter's two interfaces); d-pad hat = move,
PSX-diamond faces = Attack/Jump/Special. **If P1 is unresponsive, the pad enumerated on port 0 —
swap p1_port/p2_port in the patcher.**

## Adding a new pak
1. Get the `.pak` (archive.org / ChronoCrash / openborgames). `.rar` paks need RARLAB `unrar`
   (p7zip silently produces 0-byte files for RAR compression).
2. `mkdir openbor-<slug>` on the box; drop in `OpenBOR-7533.exe` as `OpenBOR.exe`, the pak under
   `Paks\`, and a copy of `default.cfg` as `Saves\<PAKBASE>.cfg`.
3. Add a `games.json` entry; `make deploy`. Verify fullscreen + pad on-box.

Engine source: github.com/DCurrent/openbor (release v7533).

# xp-launcher

Big-picture entertainment launcher for the home Windows XP box (`PERFORMA-7C8D5B`,
Acer AO751h — Atom Z520, GMA 500). Fullscreen console-style UI so the box never has
to look like Windows XP. Built and deployed entirely from the Linux laptop.

Design history and the wilder ideas that lost to this one:
`~/projects/personal/maintenance/xp-launcher-ideas.md`. Box infrastructure (SSH,
share, screenshots): `~/projects/personal/maintenance/windows-xp-pc.md`.

## How it works

- **C + SDL2**, cross-compiled with `i686-w64-mingw32-gcc` (SDL2 2.32.x MinGW build
  vendored in `vendor/SDL2` — the last SDL major that still targets XP).
- **Software renderer** on a fullscreen-desktop window: driver-safe on the cursed
  Poulsbo GPU, and visible to GDI capture so `xpshot` screenshots work.
- Text is pre-rendered on Linux (`scripts/gen-assets.py`, Pillow) into color-keyed
  BMPs — no font library on the XP side. **No antialiasing** in labels: AA edges
  blend toward the magenta key color and leave a pink fringe.
- Deploy = copy over the SMB share; run/kill/verify remotely via `xprun`/`xpshot`.

## Workflow

```bash
make            # build exe + assets
make deploy     # kill running instance (XP locks the exe), copy to the share
make run        # start it on the TV + screenshot to /tmp/launcher-shot.png
make kill       # stop it
```

## Controls (v0)

| Input | Action |
|-------|--------|
| ← / → (or d-pad / left stick) | move selection |
| Esc | quit to desktop |
| Enter / A | *(nothing yet — launching is v1)* |

## Roadmap

- v1: Enter opens a section; Games list driven by a plain-text config
  (`C:\GOG Games` + desktop launchers), launch + wait + regain fullscreen.
- v2: cover art tiles (rendered on the laptop, shipped as BMPs), music/movies
  sections wired to foobar2000 / MPC-HC.
- v3: boot-chain "cosplay" (shell replacement + attract mode) — prerequisite:
  convert freeSSHd/VNC to real services first (see ideas doc, safety notes).
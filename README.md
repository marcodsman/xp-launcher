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

## Controls (v1)

| Input | Action |
|-------|--------|
| ← / → / ↑ / ↓ (or d-pad / left stick) | move selection |
| Enter / Space (or pad button A) | open section / launch game |
| Esc (or pad button B) | back / quit to desktop |

Games/Music/Movies come from `games.json` (laptop side) → `assets/games.cfg`
(box side). The launcher minimizes while a game runs and takes the screen
back when it exits (verified with Quake 2).

## Remote control (for Claude / scripting)

The launcher polls `ctl.txt` next to its exe ~5×/s and executes one command:
`left right up down enter back quit`. Drop it over the share:

```bash
echo enter > /media/Acer_Notebook/launcher/ctl.txt
```

This exists because nircmd's synthetic Enter/Esc/Space reach SDL with a null
scancode and get dropped (arrows survive) — and it doubles as the automation
hook (the laptop can drive the whole UI).

## Starting it on the box

An **"Entertainment System"** shortcut (green gamepad icon, embedded via
windres) lives on the XP desktop, in the Start Menu, and in **Startup** — so
it also comes up on boot. The exe is single-instance: running the shortcut
while it's already open just restores/focuses the running one, so it's
always safe to click. Recreate the shortcuts anytime with `make shortcuts`
(runs `scripts/mklnk.vbs` on the box; uses the All Users folders).

To get back to it from the desktop without a mouse: **Ctrl+Esc** (Start
menu) → it's pinned right there, or click its taskbar button.

## Rendering model

Every screen state is a full 1024x768 BMP pre-rendered by `gen-assets.py`
at 2x and LANCZOS-downscaled (free supersampled AA): gradients, glow, drawn
icons, type. The box blits exactly one texture per frame — the Atom never
composites anything. Adding a game = edit `games.json`, `make deploy`.

## Pending games

- **SuperTux** — a modern build (worlds, powerups, editor) is in progress at
  `../supertux-xp`, cross-compiled for XP with the SDL/D3D9 renderer (OpenGL is dead on
  the GMA500). **TODO:** once `supertux2.exe` builds and runs on the box, add a `games.json`
  entry for it and `make deploy`. The old featherweight SuperTux 0.1.3 is already installed
  at `C:\supertux` as a fallback that runs today.

## Roadmap

- v2: cover art on the list screen (rendered on the laptop, shipped in the
  state BMPs), attract mode on idle, "CONTINUE?" last-played row.
- v3: boot-chain "cosplay" (shell replacement + fake console boot video) —
  prerequisite: convert freeSSHd/VNC to real services first (see ideas doc,
  safety notes).
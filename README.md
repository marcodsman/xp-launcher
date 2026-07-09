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
- Every screen is pre-rendered on Linux (`scripts/gen-assets.py`, Pillow) into full
  1024×768 BMPs at 2× (free supersampled AA) — no font/compositing on the XP side.
- **Audio** via SDL2_mixer (`vendor/SDL2`, XP-clean): UI blips + an ambient bed
  (`scripts/gen-sounds.py`) and a native in-launcher **music player** (see below).
- Deploy = copy over the SMB share; run/kill/verify remotely via `xprun`/`xpshot`.

## Workflow

```bash
make            # build exe + assets
make deploy     # kill running instance (XP locks the exe), copy to the share
make run        # start it on the TV + screenshot to /tmp/launcher-shot.png
make kill       # stop it
```

## Controls

| Input | Action |
|-------|--------|
| ← / → / ↑ / ↓ (d-pad / left stick) | move selection |
| Enter / Space (pad A) | open section / launch game / play song / play-pause |
| Esc (pad B) | back / quit to desktop |
| SELECT (pad Back) | summon the launcher back over a running game |

## Sections & screens

- **Games** — a scrolling cover-art list; a big featured hero panel updates per
  selection. Launching a game minimizes the launcher and reclaims the screen on
  exit; SELECT summons it back over a running game.
- **Movies** — browse video files scanned from `C:\XP_Share\media\movies`; selecting
  one hands off to MPC-HC (video wants the whole screen).
- **Music** — browse tracks from `C:\XP_Share\media\music`, then a native, fully
  featured **Now Playing** screen. Playback is **in-process** (SDL2_mixer), so it
  keeps going while you browse elsewhere. The screen shows an on-screen **controller
  legend** — the four face buttons drawn in their real PlayStation diamond
  (colours/shapes) so it's obvious what each does:
  **✕ Play/Pause · □ Shuffle · △ Repeat (off→all→one) · ○ Back**; **d-pad ◀▶** skips
  tracks, **d-pad ▲▼** is volume, **L1/R1** seek ±10s. Live overlays: progress bar +
  playhead, volume meter, a ▶/❚❚ state glyph, and lit shuffle/repeat buttons.
  Formats: **MP3 / WAV / OGG / FLAC / Opus** all work with the bundled
  `SDL2_mixer.dll` (MP3 via the built-in **minimp3** decoder — no external DLL).
- **Attract mode** — after 45 s idle the box cycles the library as full-bleed art
  frames with PRESS START; any input wakes it (and is consumed, so it doesn't also
  navigate). Music keeps playing underneath.

Content comes from `games.json` + the `media/` folders (laptop side) →
`assets/games.cfg` (box side). Add a game = edit `games.json`, `make deploy`.
Real box art is a drop-in: put `covers/<slug>.{jpg,png}` and regenerate; otherwise
a designed procedural cover is used. All 14 games have real art: `scripts/fetch-covers.py`
pulls official portrait art from **Steam** (Quake 2, Grim Fandango, Abe's) and **GOG**
(the Mortal Kombats, Jazz2, Heart of Darkness, Defiance) — GOG is the box's actual store,
so the art matches; `scripts/compose-covers.py` builds covers for the three open-source
games from their own official assets (SuperTux icon+logo, the SRB2 key-art banner, and a
custom voxel scene for xp-craft). Sample media for testing lives in `media-samples/`
(incl. a real MP3); `make deploy-media` pushes it to `C:\XP_Share\media` when you're home.

## Remote control (for Claude / scripting)

The launcher polls `ctl.txt` next to its exe ~5×/s and executes one command:
`left right up down enter back show quit`. Drop it over the share:

```bash
echo enter > /media/Acer_Notebook/launcher/ctl.txt
```

This exists because nircmd's synthetic Enter/Esc/Space reach SDL with a null
scancode and get dropped (arrows survive) — and it doubles as the automation
hook (the laptop can drive the whole UI). ctl input is **exempt from the
foreground gate** (it's trusted/local) and is consumed by truncating the file
to empty, *not* deleting it — delete+recreate churns the SMB directory-entry
cache and the box intermittently drops commands. Still, back-to-back commands
want ~2–3 s between them over the share.

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
icons, type, covers. The box blits exactly one texture per frame — the Atom
never composites anything. The only *live* drawing is the Now-Playing progress
bar (SDL primitives over the BMP). Rendering uses `SDL_RenderSetLogicalSize(1024,
768)` so BMPs and overlays stay aligned at any TV resolution.

Asset scripts: `gen-assets.py` (screens + `games.cfg`), `gen-sounds.py` (UI
blips + ambient), `gen-music.py` (royalty-free sample tracks), `gen-icon.py`
(exe icon). `make` runs the first two; `make deploy` ships exe + SDL2 +
SDL2_mixer DLLs + `assets/` (BMPs, `games.cfg`, `snd/`).

## Pending games

- ✅ **SuperTux** — DONE. Modern build (worlds, powerups, editor) cross-compiled for XP at
  `../supertux-xp` with the SDL/D3D9 renderer; runs fullscreen (~176 FPS menu), single-player
  patched, in the launcher. (Old 0.1.3 still at `C:\supertux` as a fallback.)
- ✅ **OpenTyrian** — DONE. Drop-in prebuilt XP binary (SDL 1.2 / DirectDraw) at
  `C:\XP_Share\opentyrian`; freeware Tyrian 2.1 data bundled, gamepad-native, in the launcher.

### Couch-game candidates to try next (curated 2026-07-05)

Selection rule for this box (Atom Z520 + GMA500, proven): **SDL2 `SDL_Renderer`→D3D9 or
DirectDraw 2D flies; raw OpenGL is dead (~2 FPS software); heavy 3D is out (Minetest ~9-13 FPS).**
Prefer native gamepad support.

**Tier A — likely drop-in (prebuilt XP binary, like SuperTux 0.1.3):**
- ✅ **OpenBOR** — DONE (2026-07-09). 2011 SDL 1.2 build (XP-safe) at `C:\XP_Share\openbor`
  running freeware **Beats of Rage** (`BOR.PAK`). Pad works natively (co-op — two pads seen).
  Windowed for now; fullscreen is the in-game Options→Video toggle (binary `BOR.cfg`). The
  engine runs a huge free `.pak` library → each future game gets its own `games.json` entry.
- **Cave Story (NXEngine-evo)** ⭐ — beloved platformer, pad-native, **freeware data bundled**,
  runs on a potato.
- ✅ **OpenTyrian** — DONE (see above). Vertical shmup, tiny, pad-perfect drop-in.
- **Abuse** — run-'n-gun, SDL, old XP builds.
- **Secret Maryo Chronicles** (old SDL 1.2 SMC — *not* the new SFML/OpenGL TSC) — Mario clone.
- ✅ **Doom** — DONE (2026-07-09). **Crispy Doom 5.1** (last XP-compatible build; 5.12's
  newer SDL2 fails to load on XP) + **Freedoom** data at `C:\XP_Share\doom`, software-rendered
  (GMA500-safe), fullscreen verified on-box. Pad pre-configured PSX-style; needs a couch
  confirm (analog LED on). *not* GZDoom — that's OpenGL = dead here.

**Tier B — worth a SuperTux-style cross-compile (SDL2→D3D9, process now proven):**
- **DevilutionX (Diablo 1)** ⭐ — SDL2, superb controller support (built for handhelds),
  lightweight. Needs Diablo data (shareware `spawn.mpq` works). A deep couch RPG.
- **The Legend of Edgar** — SDL2 metroidvania, pad, low-spec (Parallel Realities).
- **Blob Wars: Attrition** — SDL2 run-'n-gun, pad, light (same author).
- **Hurrican** — Turrican-like run-'n-gun, SDL, pad-great.
- **Commander Genius** — SDL2 Keen platformer, pad, light.

**Don't bother (wrong fit):** heavy 3D (SuperTuxKart, Xonotic, Minetest, any Godot/Unity);
raw-OpenGL-only (most Kenta Cho shmups, OpenClonk, Chromium B.S.U.); mouse-driven (OpenRA,
OpenTTD, OpenRCT2, CorsixTH — great games, wrong input for a pad).

**Top 3 to pursue:** OpenBOR (co-op, drop-in) → Cave Story (solo platformer) → DevilutionX
(the one cross-compile worth doing). Each slots in the same way: build/grab → drop on share →
`games.json` entry → `make deploy`.

## Roadmap

- ✅ **v2 (done)**: cover-art dashboard, UI sound + ambient bed, native music player
  with Now-Playing + live progress bar (MP3/OGG/FLAC/WAV), real Movies/Music sections,
  attract mode, **cover art for all 14 games** — official box art from Steam + GOG
  (`scripts/fetch-covers.py`), and composed-from-official-assets covers for the three
  open-source ones (`scripts/compose-covers.py`), resolution-independent rendering.
- **Next / nice-to-have**: "CONTINUE?" last-played row; a batch **joy2key** mapper so
  keyboard-only games (Jazz2, Abe, Heart of Darkness) take the pad (see `GAME-AUDIT.md`);
  music shuffle/queue.
- **v3**: boot-chain "cosplay" (shell replacement + fake console boot video) —
  prerequisite: convert freeSSHd/VNC to real services first so a bad shell swap
  can't lock us out (see ideas doc, safety notes).
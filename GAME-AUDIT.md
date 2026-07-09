# Game audit — does each launcher entry actually work on the box?

Status of every `games.json` entry. "Runs" = launches and is playable.
"Pad" = does the gamepad work in-game. Closed-source keyboard-only games can
be fixed box-wide with a **joystick-to-key mapper** (JoyToKey/Xpadder) — see
memory `xp-box-gamepad`; deferred as a batch.

Legend: ✅ works · ⚠️ works with caveat · ❌ broken · ❓ untested

| Game | Runs | Pad | Notes |
|------|------|-----|-------|
| SuperTux | ✅ | ✅ | native SDL2 pad; cross-compiled for XP |
| xp-craft | ✅ | ✅ | our own game; pad-native |
| Sonic Robo Blast 2 | ✅ | ✅ | software renderer; bindings fixed via autoexec.cfg |
| Sonic Robo Blast 2 (GPU) | ✅ | ✅ | our D3D9 fork |
| Beats of Rage (OpenBOR) | ✅ | ✅ | **Added + fully fixed 2026-07-09 — pad couch-confirmed by user.** Co-op wired: P1=joy1, **P2=joy0** (the Twin USB adapter's two independent pads). OpenBOR (2011 SDL 1.2 build — XP-safe, PE32) at `C:\XP_Share\openbor` running freeware `BOR.PAK`. Config = reverse-engineered binary `Saves\BOR.cfg` (memory `openbor-borcfg-format`). **Two bugs from the first patch, both now fixed:** (1) **sideways/portrait fullscreen** — OpenBOR asks SDL for a 320×240 *fullscreen* mode; the GMA500 has no 320×240 landscape mode so it substituted a 480×640 portrait one → set `screen[0]={2,0}` so it requests **640×480** (real landscape) — **verified on-box: screenshot now 640×480 upright.** (2) **wrong pad mapping** ("action buttons did directions") — my first joystick keycode guess was wrong; the real encoding (`sdl/control.c`: `500+1+port*32+index`, order buttons→axes→hats) shows directions must be the **d-pad hat**, and the live pad is **joystick 1** (not 0). Repatched: P1=joy1, d-pad hat→move, PSX-diamond faces ✕/○/□/△→Attack/Jump/Special/Fire4, L1/R1=b4/b5, START=b9; P2-P4 unbound. **⚠️ until Marco couch-confirms the pad feels right.** Engine runs a big free `.pak` library → future games get their own entries. |
| Doom (Crispy Doom + Freedoom) | ✅ | ❓ | **Added 2026-07-09.** Crispy Doom **5.1** (last XP-compatible build — 5.12 fails to load on XP: newer SDL2) + Freedoom data, software-rendered (GMA500-safe). **Fullscreen verified on-box; launches from the launcher tile.** Pad pre-configured PSX-style (Fire=✕ Use=△ Run=○ Strafe=▢, L1/R1 strafe, L2/R2 weapons, Start=menu, Select=map) in `default.cfg`/`crispy-doom.cfg`, matched to the Twin USB pad by GUID. **Pad-detection bug found + fixed (2026-07-09):** Crispy 5.1's bundled SDL2 computes a different joystick GUID than the launcher's newer SDL2 (which generated `gamecontrollerdb.txt`), so it reported "device not found". Fix = swapped the launcher's SDL2.dll into `C:\XP_Share\doom` (backup `SDL2.dll.crispy51-orig`); Crispy now logs `I_InitJoystick: Twin USB Gamepad`. **Button-feel still needs a couch test (analog LED ON — movement is on the left stick).** |
| OpenTyrian | ✅ | ✅ | **Fixed — confirmed by user 2026-07-09:** runs fullscreen and the controller works perfectly. (Earlier 2026-07-07 it was windowed w/ wrong pad mapping; resolved on-box.) |
| Quake 2 | ✅ | ❓ | runs; pad in-game not verified |
| Jazz Jackrabbit 2 | ⚠️ | ❌ | runs windowed (640×480); keyboard only. **JJ2+ (XP-native, adds real pad support) is the right fix — but blocked (2026-07-09):** both GOG exes on the box (TSF/1.24 + the "CC" 1.23) are repacks the plusifier rejects as "Unsupported version". Unlock = the **"Jazz Jackrabbit 2 with JJ2+" offline installer from GOG Extras** (needs Marco's GOG login) or Galaxy beta channel "JJ2+ Mod". Jazz² Resurrection (open-source, native pad) needs Win7+/OpenGL3.3 → **won't run on XP/GMA500**. Payload staged at `C:\XP_Share\jj2plus`. See memory `xp-box-gamepad`. |
| Abe's Oddysee | ⚠️ | ❌ | runs; keyboard only. R.E.L.I.V.E./`alive_reversing` (open-source Oddworld engine, native pad) targets Win10 → **won't run on XP** → pragmatic fix is **joy2key**. |
| ~~Metal Slug 4~~ | ❌ | — | **REMOVED 2026-07-05** — crashes instantly with a "couldn't connect to <site>" error. A GOG release is DRM-free, so this is a repacked/cracked build phoning home. Suspect; pulled from `games.json`. |
| Mortal Kombat (DOSBox) | ❌ | ❓ | **DOSBox opens to a black screen** (user, 2026-07-05). Hypothesis: our `cwd` = the `DOSBOX\` subfolder + `-conf "..\*.conf"` makes the conf's `mount c .` mount the wrong dir. Try `cwd` = the **game root** with confs referenced without `..\`. Likely fixes MK2/MK3 too (same pattern). |
| Mortal Kombat II (DOSBox) | ⚠️ | ❌ | **Runs (not black) but takes NO input — keyboard or pad** (user, 2026-07-05). Different failure from MK1. Even keyboard dead → likely a DOSBox focus/`usescancodes` config issue, not just missing pad support (joy2key alone won't fix it). |
| Mortal Kombat 3 (DOSBox) | 🚫 disabled | — | **Disabled 2026-07-09** (`"disabled": true`) — dies immediately (confirmed again by user). Third distinct DOSBox failure (MK1 black, MK2 no-input, MK3 crash) — the DOSBox packages/config on this box need a proper look before re-enabling. |
| Mortal Kombat 4 | ✅ | ⚠️ | **Runs fine, controller works — but mapping is weird** (user, 2026-07-05). Native `mk4.exe`, not DOSBox. Fixable via the game's own control config (like SRB2). |
| Mortal Kombat Trilogy | 🚫 disabled | — | **Disabled 2026-07-09** (`"disabled": true`) — weird focus / background-contention issue (matches the earlier "unplayably slow" report). Recheck on a clean box (suspect something else stealing focus/CPU); re-enable once fixed. |
| The Heart of Darkness | ⚠️ | ❌ | Runs windowed; **doesn't detect the controller** (user, 2026-07-05) → keyboard-only, joy2key case. |
| Grim Fandango | ❌ | — | **Shows an error on screen** (user, 2026-07-05). GrimE engine — likely a DirectX/display init failure on the GMA500. Investigate (dgVoodoo/software wrapper?) or remove. |
| Legacy of Kain: Defiance | ❓ | ❓ | 2003 D3D — GMA500 risk, likely won't render; verify |

## Source-port / decomp survey for the misbehaving games (2026-07-09)

Every candidate filtered through the box's hardware wall (Atom Z520 + GMA500 + XP:
raw OpenGL any version ≈ 2 FPS = dead; SDL 1.2 / SDL2→D3D9/DirectDraw / software 2D = fine).

**Real, viable fixes found (worth doing):**
- **The Heart of Darkness → `hode`** (github.com/cyxx/hode) — ⭐ clean win. Faithful reimpl,
  **SDL2 → D3D9** (the box's fast path, *not* GL), light 2D, **opens a gamepad automatically**
  (`SDL_GameControllerOpen`) → directly fixes the "no controller" bug. Needs the retail HOD
  data files (`hod.paf`, `setup.dat`, `*_hod.lvl/.sss/.mst`). Build/grab a Win32 SDL2 binary,
  set fullscreen in `hode.ini`. **Best ROI of the whole survey.**
- **Grim Fandango → ScummVM** (GrimE, ex-ResidualVM). Use the **Windows XP** build (2.7.0
  known-good; XP was *not* dropped — only Win2000 was) with the **software (TinyGL) renderer**
  so the dead GMA500 GL is never touched. Native gamepad (Controls tab). Needs the owned
  LucasArts data. Only risk = software-3D framerate on the Atom → **perf-test to confirm**,
  but it's the correct path (the current on-screen error goes away).
- **Metal Slug 4 → OpenBOR freeware paks** (fits the OpenBOR expansion below): *Metal Slug
  Beat Em Up*, *…Resistance*, *…Counter* — free, self-contained `.pak`, gamepad-native, 2D,
  engine already proven on the box. (For the user's own legally-dumped Neo-Geo carts:
  **WinKawaks 1.65** — XP-native, DirectDraw, DirectInput pad, full-speed on this class of CPU.)
- **Contra → Nestopia (or FCEUX) on XP** — the `nes-contra-us` repo is a NES *disassembly*
  that rebuilds a `.nes` ROM (needs the user's own cart dump); the realistic path is just
  running that ROM in a lightweight XP NES emulator (DirectDraw, DirectInput pad, trivially
  full-speed on the Atom). No PC port involved.

**Dead ends (don't chase):**
- **Jazz2 decomp** (Mustafa1177) — a **Ghidra project dump**, not a reimplementation. Nothing
  to build/run. JJ2+ (blocked on the GOG Extra) remains the only real pad fix for JJ2.
- **MK Trilogy "decompressor"** (Nightshades1) — **README-only research notes**, no code. The
  box's MK Trilogy focus/slowdown is a separate XP compat-shim task, unrelated to this repo.
- **Carmageddon → dethrace** — its default *software* renderer would technically run (GL is
  opt-in/off), but there's **no controller support yet** (open issue) and CPU 3D driving is
  too heavy for a 1.3 GHz in-order Atom. Skip until it matures.
- **Abe's Oddysee → R.E.L.I.V.E.** — Win10 target; stays a joy2key case (unchanged).

**New couch candidates surfaced from awesome-game-decompilations (all 2D, SDL, pad-native, no modern GL):**
- **Cave Story → CSE2** (gameblabla low-end fork) — free assets, best overall fit. ⭐
- **3D Pinball Space Cadet → SpaceCadetPinball** — SDL2→D3D9, needs the freely-available `pinball.dat`.
- **Diablo → DevilutionX** — SDL1.2-buildable, superb pad UX; shareware `spawn.mpq` works
  (verify XP build on-box). (Explicitly DEAD: RigelEngine, wipeout-rewrite, TRX/Tomb Raider,
  re-plants-vs-zombies — all require OpenGL 3.x.)

## To do (deferred — user is spot-checking manually)
- **Co-op / P2 across all games** (user, 2026-07-09): OpenBOR P2 was dead until bound to the
  2nd pad (joy0). Audit every 2-player-capable game for the same — the Twin USB shows as two
  independent joysticks (P1=joy1, P2=joy0); make sure each game's P2 is on the other pad.
- Confirm/kill the ❓ rows.
- Metal Slug 4: diagnose (missing dep? config?) or remove from the launcher.
- Batch joy2key: pick a mapper, write one pad→keyboard profile, have the
  launcher spawn it alongside the keyboard-only entries (per-game flag in
  games.json, e.g. `"joy2key": true`).
- Remove anything that stays ❌ from `games.json`.

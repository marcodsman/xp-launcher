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
| Beats of Rage (OpenBOR) | ⚠️ | ✅ | **Added 2026-07-09.** OpenBOR (2011 SDL 1.2 build — XP-safe, PE32; the new 4.0 is unverified on XP) at `C:\XP_Share\openbor` running the freeware `BOR.PAK`. **Pad works natively** — the log detects the Twin USB adapter as **two** gamepads, so **2-player co-op** is free. **Only gap: opens windowed (320×240).** Fullscreen lives in the binary `Saves\BOR.cfg` (Options→Video→Display Mode in-game); set it once on-box, then capture the cfg to `config/openbor/`. Engine runs many free `.pak` games → future ones become their own launcher entries. |
| Doom (Crispy Doom + Freedoom) | ✅ | ❓ | **Added 2026-07-09.** Crispy Doom **5.1** (last XP-compatible build — 5.12 fails to load on XP: newer SDL2) + Freedoom data, software-rendered (GMA500-safe). **Fullscreen verified on-box; launches from the launcher tile.** Pad pre-configured PSX-style (Fire=✕ Use=△ Run=○ Strafe=▢, L1/R1 strafe, L2/R2 weapons, Start=menu, Select=map) in `default.cfg`/`crispy-doom.cfg`, matched to the Twin USB pad by GUID. **Pad-detection bug found + fixed (2026-07-09):** Crispy 5.1's bundled SDL2 computes a different joystick GUID than the launcher's newer SDL2 (which generated `gamecontrollerdb.txt`), so it reported "device not found". Fix = swapped the launcher's SDL2.dll into `C:\XP_Share\doom` (backup `SDL2.dll.crispy51-orig`); Crispy now logs `I_InitJoystick: Twin USB Gamepad`. **Button-feel still needs a couch test (analog LED ON — movement is on the left stick).** |
| OpenTyrian | ✅ | ✅ | **Fixed — confirmed by user 2026-07-09:** runs fullscreen and the controller works perfectly. (Earlier 2026-07-07 it was windowed w/ wrong pad mapping; resolved on-box.) |
| Quake 2 | ✅ | ❓ | runs; pad in-game not verified |
| Jazz Jackrabbit 2 | ⚠️ | ❌ | runs windowed (640×480); keyboard only. **JJ2+ (XP-native, adds real pad support) is the right fix — but blocked (2026-07-09):** both GOG exes on the box (TSF/1.24 + the "CC" 1.23) are repacks the plusifier rejects as "Unsupported version". Unlock = the **"Jazz Jackrabbit 2 with JJ2+" offline installer from GOG Extras** (needs Marco's GOG login) or Galaxy beta channel "JJ2+ Mod". Jazz² Resurrection (open-source, native pad) needs Win7+/OpenGL3.3 → **won't run on XP/GMA500**. Payload staged at `C:\XP_Share\jj2plus`. See memory `xp-box-gamepad`. |
| Abe's Oddysee | ⚠️ | ❌ | runs; keyboard only. R.E.L.I.V.E./`alive_reversing` (open-source Oddworld engine, native pad) targets Win10 → **won't run on XP** → pragmatic fix is **joy2key**. |
| ~~Metal Slug 4~~ | ❌ | — | **REMOVED 2026-07-05** — crashes instantly with a "couldn't connect to <site>" error. A GOG release is DRM-free, so this is a repacked/cracked build phoning home. Suspect; pulled from `games.json`. |
| Mortal Kombat (DOSBox) | ❌ | ❓ | **DOSBox opens to a black screen** (user, 2026-07-05). Hypothesis: our `cwd` = the `DOSBOX\` subfolder + `-conf "..\*.conf"` makes the conf's `mount c .` mount the wrong dir. Try `cwd` = the **game root** with confs referenced without `..\`. Likely fixes MK2/MK3 too (same pattern). |
| Mortal Kombat II (DOSBox) | ⚠️ | ❌ | **Runs (not black) but takes NO input — keyboard or pad** (user, 2026-07-05). Different failure from MK1. Even keyboard dead → likely a DOSBox focus/`usescancodes` config issue, not just missing pad support (joy2key alone won't fix it). |
| Mortal Kombat 3 (DOSBox) | ❌ | — | **Crashes immediately** (user, 2026-07-05). Third distinct DOSBox failure (MK1 black, MK2 no-input, MK3 crash) — the DOSBox packages/config on this box need a proper look. |
| Mortal Kombat 4 | ✅ | ⚠️ | **Runs fine, controller works — but mapping is weird** (user, 2026-07-05). Native `mk4.exe`, not DOSBox. Fixable via the game's own control config (like SRB2). |
| Mortal Kombat Trilogy | ⚠️ | ❓ | **Unplayably slow** (user, 2026-07-05) — possibly temporary (background contention?); recheck on a clean box. |
| The Heart of Darkness | ⚠️ | ❌ | Runs windowed; **doesn't detect the controller** (user, 2026-07-05) → keyboard-only, joy2key case. |
| Grim Fandango | ❌ | — | **Shows an error on screen** (user, 2026-07-05). GrimE engine — likely a DirectX/display init failure on the GMA500. Investigate (dgVoodoo/software wrapper?) or remove. |
| Legacy of Kain: Defiance | ❓ | ❓ | 2003 D3D — GMA500 risk, likely won't render; verify |

## To do (deferred — user is spot-checking manually)
- Confirm/kill the ❓ rows.
- Metal Slug 4: diagnose (missing dep? config?) or remove from the launcher.
- Batch joy2key: pick a mapper, write one pad→keyboard profile, have the
  launcher spawn it alongside the keyboard-only entries (per-game flag in
  games.json, e.g. `"joy2key": true`).
- Remove anything that stays ❌ from `games.json`.

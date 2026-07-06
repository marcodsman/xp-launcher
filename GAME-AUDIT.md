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
| Quake 2 | ✅ | ❓ | runs; pad in-game not verified |
| Jazz Jackrabbit 2 | ⚠️ | ❌ | runs windowed (640×480); **keyboard only, no pad** → joy2key |
| Abe's Oddysee | ⚠️ | ❌ | runs; **keyboard only, no pad** → joy2key |
| ~~Metal Slug 4~~ | ❌ | — | **REMOVED 2026-07-05** — crashes instantly with a "couldn't connect to <site>" error. A GOG release is DRM-free, so this is a repacked/cracked build phoning home. Suspect; pulled from `games.json`. |
| Mortal Kombat (DOSBox) | ❌ | ❓ | **DOSBox opens to a black screen** (user, 2026-07-05). Hypothesis: our `cwd` = the `DOSBOX\` subfolder + `-conf "..\*.conf"` makes the conf's `mount c .` mount the wrong dir. Try `cwd` = the **game root** with confs referenced without `..\`. Likely fixes MK2/MK3 too (same pattern). |
| Mortal Kombat II (DOSBox) | ⚠️ | ❌ | **Runs (not black) but takes NO input — keyboard or pad** (user, 2026-07-05). Different failure from MK1. Even keyboard dead → likely a DOSBox focus/`usescancodes` config issue, not just missing pad support (joy2key alone won't fix it). |
| Mortal Kombat 3 (DOSBox) | ❌ | — | **Crashes immediately** (user, 2026-07-05). Third distinct DOSBox failure (MK1 black, MK2 no-input, MK3 crash) — the DOSBox packages/config on this box need a proper look. |
| Mortal Kombat 4 | ✅ | ⚠️ | **Runs fine, controller works — but mapping is weird** (user, 2026-07-05). Native `mk4.exe`, not DOSBox. Fixable via the game's own control config (like SRB2). |
| Mortal Kombat Trilogy | ❓ | ❓ | |
| The Heart of Darkness | ❓ | ❓ | |
| Grim Fandango | ❓ | ❓ | |
| Legacy of Kain: Defiance | ❓ | ❓ | 2003 D3D — GMA500 risk, likely won't render; verify |

## To do (deferred — user is spot-checking manually)
- Confirm/kill the ❓ rows.
- Metal Slug 4: diagnose (missing dep? config?) or remove from the launcher.
- Batch joy2key: pick a mapper, write one pad→keyboard profile, have the
  launcher spawn it alongside the keyboard-only entries (per-game flag in
  games.json, e.g. `"joy2key": true`).
- Remove anything that stays ❌ from `games.json`.

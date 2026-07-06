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
| Metal Slug 4 | ❌ | — | **does not launch at all** (user, 2026-07-05) — investigate or remove |
| Mortal Kombat (DOSBox) | ❓ | ❓ | DOSBox — pad likely needs DOSBox joy config or joy2key |
| Mortal Kombat II (DOSBox) | ❓ | ❓ | " |
| Mortal Kombat 3 (DOSBox) | ❓ | ❓ | " |
| Mortal Kombat 4 | ❓ | ❓ | D3D — GMA500 risk, verify it renders |
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

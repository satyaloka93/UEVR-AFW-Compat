---
type: playbook
title: Comparing SH2 AFW and Native-era profile variants
description: A scoped SHProto case study for comparing divergent AppData profiles — AFW config, an independent uevr_utils script-stack migration, and full-profile preservation; not a universal AFW conversion recipe.
tags:
- afw
- profile
- config
- uevr-utils
timestamp: '2026-07-27T09:30:00+09:00'
---

# Reference example and scope

`SHProto-Win64-Shipping` (live, AFW-enhanced) vs `SHProto-Win64-Shipping_mine`
(Native-era backup, 2026-07-15 pre-stable-RT-fix).

This comparison records what changed in **this SH2 profile family**. AFW does
not inherently require uevr_utils, replacement scripts, or
`VR_NativeStereoFix=false`: Avowed keeps Native Stereo Fix enabled, while TOW2
requires it disabled. Reuse the comparison method and preservation discipline;
port settings only after per-game validation.

# 1. SH2 config pairing

The validated unified-branch SH2 profile may remain saved as:

```text
VR_RenderingMethod=3
VR_NativeStereoFix=false
```

At branch tip `a3d3128c`, the saved AFW value is **not** the cold-start method:
commit `75c172c5` forces SHProto to Native in memory before OpenXR swapchain
creation. After Native stabilizes, the user may switch to the saved AFW mode.
This distinction is essential when comparing the profile with startup logs.

Supporting changes seen in the SH2 conversion (apply per taste, not required):

- `VR_ControllerPitchOffset=0` (framework owns aim; the old profile's tuned
  `-20.812` offset belongs to the classic script)
- `UI_Distance=2.0` / `UI_Size=2.0`
- `VR_RoomscaleMovementSweep=true`
- `LuaLoader_LogToDisk=true` while the profile is under development
- Debug/recenter hotkeys unbound (`-1`) once stable

**Directional mode-switch rule:** AFW → Native live switching is unsafe.
SH2 now starts Native by code and has a promising runtime Native → AFW result
on the exact shipped checkpoint. Follow the [SH2 game concept](../games/silent-hill-2.md)
rather than interpreting the saved `VR_RenderingMethod=3` as AFW cold-start;
follow the [narrow-port decision](../decisions/narrow-port-scope.md) for other games.

# 2. Independent script-stack migration

The live SH2 profile also replaces the classic single-script profile with the
modular **uevr_utils framework**. This migration happened alongside AFW but is
not an AFW dependency:

| | Classic (Native-era) | AFW-enhanced |
|---|---|---|
| Entry | one big `camera.lua` (~1.9k lines, direct class hooks) | `main.lua` + feature scripts (`melee.lua`) |
| Libraries | none | `scripts/libs/` (~35 modules: hands, ik, gestures, gunstock, input, interaction…) |
| Tuning | constants in-script | `data/*.json` parameter files (e.g. `sh2_config.json`) |
| Native plugins | game plugin only (`sh2r.dll`) | + `uevr_utils.dll` |
| CVar tuning | `cvars_*.txt` | small `user_script.txt` (SH2: shadow caching + `r.Lumen.ScreenProbeGather.DownsampleFactor 13`) |

Conversion details that matter:

- **Empty, don't delete, the old entry script** — live `camera.lua` is 0 bytes
  so the loader finds it but runs nothing; the framework takes over. Keeps the
  old path trivially restorable.
- The classic profile's OpenVR binding JSONs (`actions.json`,
  `bindings_knuckles.json`, …) and `shader_overrides/` stay only in the backup
  if the AFW profile runs OpenXR.
- **Known framework pitfall:** `libs/input.lua` `updatePawnSettings` assumes
  reflected APawn properties (`bUseControllerRotationPitch`) that some games'
  pawn classes lack — `pcall`-guard those reads/writes (fixed this way for SHf,
  [/games/silent-hill-f.md](../games/silent-hill-f.md); the same libs tree ships
  in the SH2 profile).

# 3. Preservation discipline for divergent variants

Follow [/playbooks/checkpoint-and-recovery.md](checkpoint-and-recovery.md):

- Keep the full pre-AFW profile as a sibling directory (`<Game>_mine`,
  `<Game>_hands`), not just a `config.txt.bak` — the script stacks diverge too
  much for a config-only restore.
- The backup keeps its own breadcrumbs (dated `.bak`s, preserved
  `log.poor_perf_*.txt`) — leave them in place; they date the snapshot.
- Hash-pin whichever variant is currently known-good before the next
  experiment.

# Related

- [/projects/puredark-afw-integration.md](../projects/puredark-afw-integration.md) — backend side of AFW
- [/fixes/afw-beta4-motion-vector-scale.md](../fixes/afw-beta4-motion-vector-scale.md) — runtime/ghosting state
- [/games/silent-hill-f.md](../games/silent-hill-f.md) — same framework libs, pcall fix

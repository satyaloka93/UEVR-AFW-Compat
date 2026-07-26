---
type: game-profile
title: Silent Hill 2 (SHProto) — generic-path title unified onto the AFW fork
description: SH2 has no per-game carve-out and runs on baseline UEVR's generic shared path; on the AFW-Compat fork it works once the hardened UE5.8 UESDK is dropped and it is forced to cold-start in Native (runtime AFW switch works). Distinct game from Silent Hill f.
tags:
- silent-hill-2
- sh2
- shproto
- ue5
- openxr
- d3d12
- native-stereo
- afw
- unification
timestamp: '2026-07-26T18:50:00+09:00'
---

# Identity

- Executable: `SHProto-Win64-Shipping.exe` (Silent Hill 2 remake; codename SHProto).
- **Distinct from Silent Hill f** (`SHf-Win64-Shipping.exe`) — different game, different fixes.
  Do not conflate; the fork's `is_silent_hill_f_executable` gates are SHf, not SH2.
- Renderer/runtime: D3D12 + OpenXR.

# Working state

- **Baseline / Joey-Merged (`021d45b7`, `RDC-Joey-Merged`):** works reliably, zero game-specific
  code — SH2 is a well-behaved UE5 title that runs on the generic shared stereo/present path.
- **PureDark plain beta.4 bundle:** works (Native or AFW).
- **UEVR-AFW-Compat fork (unified backend):** works after the
  [SH2 AFW cold-start / Native-force fix](/fixes/sh2-shproto-afw-cold-start-native-force.md) —
  drop the hardened UE5.8 UESDK and force Native cold-start. Confirmed 2026-07-26 across
  repeated launches; AFW works when switched to at runtime and reverts to Native on quit.

# Known-good config (fork, unified backend)

- `VR_RenderingMethod=0` (Native at cold-start) — enforced in code for SHProto; do not cold-start
  in AFW. Switch to AFW at runtime if desired.
- `VR_NativeStereoFix=false` — SH2 needs no Native Stereo Fix; enabling it blacks out the right
  eye (SH2 is not in the Avowed/SHf direct-pose gate).
- Inject once the game can render a real 3D frame; the stereo device / OpenXR swapchains come up
  after that, not at the 2D tile/title screen (a `Failed to find stereo rendering device` 2×
  fail-then-recover is normal — it also appears in the working logs).

# Rejected / cautions

- **Hardened UE5.8 UESDK (`9034a857`)** — GPU-hangs SH2 (TDR/reboot, no minidump). SH2 needs the
  baseline UESDK `491f973a`.
- **Cold-start in AFW** — hangs VR init; SH2 has no carve-out for the reworked AFW bring-up path.
- **`VR_NativeStereoFix=true`** — right-eye black; not needed for SH2.

# Relationships

- Fix: [/fixes/sh2-shproto-afw-cold-start-native-force.md](/fixes/sh2-shproto-afw-cold-start-native-force.md)
- Project: [/projects/puredark-afw-integration.md](/projects/puredark-afw-integration.md)
- Decision: [/decisions/narrow-port-scope.md](/decisions/narrow-port-scope.md)
- Contrast: [/games/silent-hill-f.md](/games/silent-hill-f.md) (different game, has a real carve-out)

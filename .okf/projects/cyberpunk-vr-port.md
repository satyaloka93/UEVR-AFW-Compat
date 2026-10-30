---
type: Reference Project
title: CyberpunkVR Port — PSVR2 gameplay and melee-haptics reference
description: Reference implementation for semantic weapon effects, physical melee swing/impact events, and the single-owner PSVR2 actuator rule adapted by the TOW2 experiment.
resource: https://github.com/satyaloka93/cyberpunk-vr-port/tree/psvr2-tweaks
tags: [cyberpunk, psvr2, adaptive-triggers, haptics, melee, reference]
timestamp: '2026-08-13T18:40:00+09:00'
---

# Relevance to UEVR

The `psvr2-tweaks` branch is the gameplay-semantics and operational-safety
reference for [TOW2 PSVR2 per-gun and melee haptics](../fixes/tow2-psvr2-per-gun-haptics.md).
It demonstrates the architecture rather than supplying code that can be copied
unchanged into TOW2:

1. Game-specific code detects weapon state, shots, physical swings and impacts.
2. It publishes bounded semantic data rather than raw device commands.
3. A companion [PSVR2Toolkit bridge](psvr2toolkit.md) is the sole actuator
   owner and mixes all effects through one `HapticsEngine`.
4. Competing OpenXR haptics, DSX and the mod's separate UDP client remain off.

Cyberpunk-specific process names, CET/RED4ext APIs, shared-memory slots,
`DualSenseXConfig.txt` and weapon-transition semantics are not portable to
UEVR. TOW2 therefore uses UEVR Lua plus a minimal custom-event relay while
preserving the ownership and fail-safe model.

# Melee reference behavior

Cyberpunk's physical-melee path established the target distinction:

- one weapon-hand swing pulse per armed swing episode;
- swing amplitude scales from `0.45` to `0.85` with speed;
- contact emits a clearly stronger `1.0` impact pulse;
- short swing pulses use a higher carrier and longer impacts use a lower
  carrier in the bridge;
- both effects enter the same PCM mixer as firearm and optional audio effects.

Cyberpunk has a direct game collision/hit path. TOW2 reuses its
runtime-validated native downstroke for the swing, but runtime disproved native
rumble as contact evidence: it appeared after 29 of 30 attacks at a median 153
ms, including swings without sensible reported target impact. TOW2 therefore permanently rejects rumble as impact. Its follow-up bounded
tracked-weapon sweep was also runtime-rejected after zero contacts, failed tip
resolution, per-frame log spam, no felt haptics, and no improvement to native
sound/damage delay. Cyberpunk's signal must not be conflated with either TOW2
attack/animation rumble or an unvalidated generic scene sweep.

# Source checkpoint

- Reference branch: `psvr2-tweaks`.
- Observed branch checkpoint: `24ab89e5ac2ad4a2bcf4a23aaff80fb56a9ba3e0`.
- Canonical operational knowledge:
  [PSVR2 Sense adaptive triggers and grip haptics](https://github.com/satyaloka93/cyberpunk-vr-port/blob/psvr2-tweaks/.okf/operations/psvr2-adaptive-triggers.md).
- User setup guide:
  [PSVR2 adaptive-trigger setup](https://github.com/satyaloka93/cyberpunk-vr-port/blob/psvr2-tweaks/docs/PSVR2-ADAPTIVE-TRIGGERS.md).

The Cyberpunk OKF runbook is authoritative for its current tested setup. In
particular, it supersedes older category-override advice: stock per-weapon
settings behave correctly after eliminating the competing UDP owner.

# Related

- [TOW2 working state](../games/outer-worlds-2.md)
- [TOW2 PSVR2 integration](../fixes/tow2-psvr2-per-gun-haptics.md)
- [PSVR2Toolkit reference](psvr2toolkit.md)

# Citations

[1] [CyberpunkVR Port PSVR2 branch](https://github.com/satyaloka93/cyberpunk-vr-port/tree/psvr2-tweaks)
[2] [Cyberpunk PSVR2 OKF runbook](https://github.com/satyaloka93/cyberpunk-vr-port/blob/psvr2-tweaks/.okf/operations/psvr2-adaptive-triggers.md)
[3] [Cyberpunk weapon motion-haptics producer](https://github.com/satyaloka93/cyberpunk-vr-port/blob/psvr2-tweaks/mods/cet/CyberpunkVRPort_Weapon/init.lua)

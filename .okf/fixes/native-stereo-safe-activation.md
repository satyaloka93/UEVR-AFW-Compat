---
type: fix
title: Native Stereo Fix — delayed/safe activation and runtime self-disable
description: Never activate the native stereo fix during startup or load transitions; wait for a stable-frame safe point, and let it degrade/disable itself at runtime instead of crashing. Config keys VR_DelayNativeStereoFix and VR_NativeStereoFix_SkipFrameDecrement.
tags:
- native-stereo
- stability
- crash
timestamp: '2026-07-20T00:00:00Z'
resource: src/mods/vr/FFakeStereoRenderingHook.cpp
---

# Problem

The native stereo fix touches scene capture, render targets, and RHI command
hooks. During early load, world transitions, and title screens those
assumptions are invalid, producing null-derefs and crashes (Avowed startup
crashes; Hogwarts TaskGraph AVs).

# Fix components

- **Safe point:** native stereo is not considered active until the game holds a
  stable render/gameplay state (e.g. `Safe point reached after 60 stable
  frames; enabling native stereo fix`). Config: `VR_DelayNativeStereoFix=true`.
- **Load-transition passthrough:** on detected load/world transitions the fix
  drops to passthrough rather than operating on dying resources.
- **Runtime degrade/disable:** on hook or scene-capture instability the fix
  disables itself for the session (with UI status readouts) instead of
  hard-failing.
- **Skip Frame Decrement:** `VR_NativeStereoFix_SkipFrameDecrement=true` avoids
  decrementing the scene frame counter before the right-eye scene-capture pass
  (monotonic second pass); required for Avowed, harmless elsewhere.
- **VEH scoping:** the invasive XR null-deref VEH must install **only** when
  Native Stereo Fix is enabled — a globally installed VEH stalled TOW2's title
  path even with the fix off.

# Per-game applications

- [/games/avowed.md](/games/avowed.md) — core of the recovery; pairs with
  [/fixes/render-target-validation-hardening.md](/fixes/render-target-validation-hardening.md).
- [/games/hogwarts-legacy.md](/games/hogwarts-legacy.md) — needed an additional
  post-transition cooldown: [/fixes/hogwarts-transition-cooldown.md](/fixes/hogwarts-transition-cooldown.md).
- [/games/outer-worlds-2.md](/games/outer-worlds-2.md) — the fix is **rejected**
  entirely there; TOW2 runs plain Native Stereo with `VR_NativeStereoFix=false`.

# Principle

Prefer *validate-then-activate* and *degrade-not-crash* over guessing offsets
and hoping. Every crashy path here got safer by refusing to run until proven
stable, and by having a clean self-disable.

---
type: fix
title: OpenVR analog trigger fallback + binding preservation
description: Add /actions/default/in/TriggerValue analog action with fallback when digital trigger click is unreliable, map into XInput trigger values, and stop overwriting existing non-empty binding files on every launch.
tags:
- openvr
- input
- bindings
- trigger
timestamp: '2026-07-20T00:00:00Z'
resource: src/mods/VR.cpp
---

# Problem

On the OpenVR path some controllers/runtimes do not emit trigger *click*
reliably, making games unplayable (Avowed trigger failures). Separately, UEVR
rewrote binding files every launch, clobbering working custom bindings.

# Fix

In `src/mods/VR.{cpp,hpp}` and `src/mods/vr/Bindings.cpp`:

- New action `/actions/default/in/TriggerValue` (analog).
- If digital trigger state is unreliable, fall back to analog trigger value
  reads; map values directly into XInput trigger values.
- Generated bindings updated for `oculus_touch` and `knuckles` with trigger
  `pull` mapped to both `TriggerValue` and `trigger`.
- **Binding preservation:** existing non-empty binding files are no longer
  overwritten; `actions.json` may still be rewritten only if the newer
  `TriggerValue` action is missing.

# Context

This is what made the OpenVR-era Avowed profile playable —
[/games/avowed.md](/games/avowed.md). For the later OpenXR/PSVR2 input work see
[/fixes/psvr2-triangle-dpad.md](/fixes/psvr2-triangle-dpad.md).

# Citations

- `AVOWED_UEVR_UPDATE.md` §1.8–1.10

---
type: fix
title: PSVR2-on-SteamVR/OpenXR bindings (Triangle d-pad modifier, R3 roles)
description: 'Detect "SteamVR/OpenXR : playstation_vr2", mirror Triangle capacitive touch to left thumbrest for d-pad shifting (bypassing the invalid Oculus touch-inactivity gate), suppress snap turn while shifting, and split R3 short/long/L3+R3 roles.'
tags:
- psvr2
- openxr
- input
- bindings
timestamp: '2026-07-20T00:00:00Z'
resource: src/mods/VR.cpp
---

# Problem

SteamVR's PSVR2 emulation doesn't expose the inputs UEVR's d-pad-shifting
(`VR_DPadShiftingMethod=1`, LEFT_TOUCH) expects, and the default gate logic
assumes Oculus A/B-touch behaviour that PSVR2 emulation never satisfies.

# Fix

- Detect runtime/controller string `SteamVR/OpenXR : playstation_vr2`.
- Mirror left PSVR2 `BButtonTouch` (**Triangle capacitive touch**) to
  `ThumbrestTouchLeft`.
- In d-pad shifting method 1, accept Triangle touch directly and **bypass the
  Oculus A/B-touch inactivity gate**.
- Suppress snap turn while the Triangle modifier is active.
- Button roles (Avowed profile): solo `R3` short press = pause/start; solo `R3`
  long press = inventory/select/back; `L3+R3` = UEVR menu; left grip = item
  wheel.

Expected control: touch/hold Triangle and move the right stick to emit d-pad
directions.

# Notes

- Profile keys: `VR_DPadShifting=true`, `VR_DPadShiftingMethod=1`,
  `remap_lb_button=0`.
- This was also ported (d-pad path only) to the PureDark AFW branch —
  [/decisions/narrow-port-scope.md](/decisions/narrow-port-scope.md).

# Citations

- `AVOWED_CHECKPOINT_STATE.md` (behaviour summary)
- `PUREDARK_AFW_AVOWED_WORKLOG.md` — "Restore PSVR2 Triangle D-pad modifier"

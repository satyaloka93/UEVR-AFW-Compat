---
type: playbook
title: Adding PSVR2 advanced haptics + adaptive triggers to a new UE game
description: The repeatable process for giving a UEVR-supported game per-weapon PSVR2 Sense adaptive triggers and felt grip haptics — reuse the bridge/relay/protocol unchanged, write a game-specific Lua producer that classifies weapons and selects felt effects, detect fire events (including hold-to-charge), and tune against real hardware with identity logging and an in-game editor.
tags: [psvr2, haptics, adaptive-triggers, process, lua, reusable]
timestamp: '2026-08-19T00:00:00+09:00'
---

How the TOW2 result is reproduced for another game. The
[PSVR2Toolkit bridge](../projects/psvr2toolkit.md), the UEVR relay, and the
`TOW2PSVR2/1` protocol are **reusable and game-agnostic** — you rarely touch
them. The work is a per-game **Lua producer** plus tuning against real hardware,
respecting the felt-effect findings recorded in
[TOW2 PSVR2 per-gun haptics](../fixes/tow2-psvr2-per-gun-haptics.md#felt-effect-and-detection-findings-2026-08-tuning).
The architecture derives from the
[CyberpunkVR Port reference](../projects/cyberpunk-vr-port.md).

# What you reuse vs. write

- **Reuse unchanged:** `psvr2_toolkit_dsx_bridge.exe --tow2`, the UEVR relay
  plugin, the Toolkit CAPI/loader, the `TOW2PSVR2/1` protocol. The relay forwards
  by prefix only, so **new effects need no relay rebuild**.
- **Write per game:** one UEVR Lua *producer* in the game profile `scripts/` dir,
  plus per-weapon profile values.
- **Rebuild the bridge only** for a genuinely new protocol operation (rare) —
  then extend `tow2_protocol.*`, `main.cpp`, its test, and the bridge `README.md`
  in the [PSVR2Toolkit fork](../projects/psvr2toolkit.md).

# Prerequisites

1. The game already runs in UEVR (stereo working) — haptics is additive.
2. Profile sets `VR_ExternalPSVR2Haptics=true` so the backend stops forwarding
   native XInput vibration to OpenXR (the single-owner invariant — see the fix).
3. SteamVR + both Sense controllers connected; **no** other Sense owner running
   (DSX, the Cyberpunk bridge, a second bridge). Launch the bridge with `--tow2`.

# Steps

1. **Copy the reference producer** (`07_tow2_psvr2_haptics.lua`) into the new
   game's profile `scripts/` as a starting point.

2. **Find the equipped weapon** for the game's pawn (TOW2:
   `pawn:GetCurrentWeapon()`). Read `get_full_name()`,
   `get_class():get_full_name()`, and the reflected weapon-category tag. Getters
   differ per game — this is the main port cost.

3. **Add identity logging first.** Emit the `weapon` diagnostic op on every
   weapon change so name/class/tag/resolved-profile/source land in
   `tow2_bridge.log`. Do this *before* tuning — it is what reveals mis-classified
   and undetected weapons.

4. **Classify weapons → profiles.** Match the category tag first, then keyword the
   full name, then a generic fallback. Persist per-weapon overrides keyed on the
   **class full-name path** (stable across sessions and instances; the object
   name's per-spawn id is not).

5. **Detect fire events.** Start with "native rumble while trigger held", then add
   **trigger-release-edge detection** for hold-to-charge and near-silent weapons.
   Confirm each detector fires in the log before judging feel.

6. **Choose FELT effects (the core lesson).** A single grip pulse is not felt on
   the Sense grip — deliver everything as a sustained rhythm:
   - Full-auto / continuous beam → `automatic_fire` (rhythm + trigger).
   - Semi-auto / charge → a short `automatic_fire` burst.
   - Melee swing → `rhythm` … `rhythm_stop` (leaves the trigger alone).

7. **Expose an in-game tuning editor.** A UEVR script panel with per-profile
   sliders (grip amplitude, rhythm/burst rate + length, trigger position/strength,
   slope), plain-language hover tooltips (`imgui.is_item_hovered()` +
   `imgui.set_tooltip()`), a "test on Sense" button, and JSON save/load
   (`json.dump_file`/`load_file`) so the user tunes without editing Lua.

8. **Tune against hardware, per weapon class.** Compare a weak weapon to one that
   feels good and match rate/length. Grip amplitude is not the lever near 1.0 —
   rhythm-vs-pulse and rate/length are.

# Validation (before calling it done)

- Bridge Release build + TOW2/DSX protocol tests pass (only if the bridge was
  touched).
- Lua parses (luaparser / `luac -p`).
- `tow2_bridge.log` shows `tow2_parse_ok` + `tow2_apply_ok` for the emitted ops
  and `tow2_weapon` lines per equipped weapon.
- **Hands-on:** every weapon class produces a distinct, felt effect. Green
  software logs are necessary but not sufficient.
- Archive the deployed bridge + Lua with sha256 and keep pre-deploy rollback
  copies — [checkpoint and recovery](checkpoint-and-recovery.md).

# Deploy boundaries

Deploy only the two game-facing files unless the bridge changed: the bridge exe →
its run folder; the producer `*.lua` → the game profile `scripts/` (keep the
published profile copy in sync). Leave the driver, CAPI/loader, relay, and
launcher scripts untouched.

# Related

- [TOW2 PSVR2 per-gun haptics (reference implementation + findings)](../fixes/tow2-psvr2-per-gun-haptics.md)
- [PSVR2Toolkit actuator/bridge reference](../projects/psvr2toolkit.md)
- [CyberpunkVR Port reference](../projects/cyberpunk-vr-port.md)
- [The Outer Worlds 2 working state](../games/outer-worlds-2.md)
- [Staged fix methodology](staged-fix-methodology.md)

---
type: Reference Project
title: PSVR2Toolkit — Sense actuator and haptics bridge reference
description: Native PSVR2 driver/CAPI and companion bridge that exclusively actuate adaptive triggers and 3000 Hz grip PCM for the TOW2 experiment.
resource: https://github.com/satyaloka93/PSVR2Toolkit/tree/cyberpunk-dsx-bridge
tags: [psvr2, toolkit, adaptive-triggers, pcm, haptics, bridge, reference]
timestamp: '2026-08-13T18:40:00+09:00'
---

# Role in the TOW2 integration

PSVR2Toolkit is the native actuator boundary for
[TOW2 PSVR2 per-gun and melee haptics](../fixes/tow2-psvr2-per-gun-haptics.md).
The UEVR backend, Lua and relay do not issue raw Sense commands. The companion
bridge alone calls Toolkit CAPI for:

- adaptive-trigger effects;
- signed 8-bit, 3000 Hz grip PCM;
- headset rumble commands where a game integration requests them.

This enforces the same single-owner rule learned from
[CyberpunkVR Port](cyberpunk-vr-port.md). DSX, another bridge and UEVR/OpenXR
haptic application must not drive the Sense actuators concurrently.

# Bridge modes

The `psvr2_toolkit_dsx_bridge.exe` source remains one executable with mutually
exclusive runtime modes:

| Mode | Invocation | Producers and consumers |
|---|---|---|
| Cyberpunk/DSX | Normal launch, optionally `--cyberpunk-config <file>` | DSX protocol, Enhanced DualSense Support config monitoring, Cyberpunk motion shared memory and optional game-audio haptics |
| TOW2 | `--tow2` | Validated `TOW2PSVR2/1` packets from the UEVR relay; Cyberpunk-specific audio and shared-memory watchers disabled |

Both modes use the same Toolkit CAPI loader and `HapticsEngine`, but only one
bridge process may run. Both normally bind localhost UDP port `6969` and target
the same controllers.

# TOW2 protocol boundary

The TOW2 mode intentionally does not reinterpret arbitrary Lua data as DSX or
raw device structures. `tow2_protocol.cpp` validates a small versioned semantic
schema before actuation:

- heartbeat, clear and per-side off;
- bounded slope/weapon trigger profiles;
- firearm shot/pulse and automatic rhythm controls;
- `melee_swing` and `melee_impact` grip pulses. TOW2 currently emits swing only;
  impact fails closed after both rumble and tracked-weapon sweep candidates were
  runtime-rejected. The bridge remains agnostic to game collision and damage.

The bridge heartbeat timeout clears triggers and grip/rhythm state after 2.5
seconds without TOW2 packets. Shutdown clears effects before CAPI deinitializes.

# Build and checkpoint

The bridge is an x64 CMake/MSVC target under
`projects/psvr2_toolkit_dsx_bridge`. TOW2 protocol tests and existing DSX tests
are separate targets and both must pass before staging a bridge candidate.

- Reference branch: `cyberpunk-dsx-bridge`.
- Observed committed checkpoint before the uncommitted TOW2 extension:
  `5f2ded5b5105f045610ac8df1469544878f2f312`.
- Runtime-confirmed initial TOW2 firearm bridge:
  `e097e9650d76a3baf546468c327140f9e4b22442e6489d3f9945fdcdf348d7cf`.
- Static-tested TOW2 melee semantic bridge candidate:
  `db4c8b344cbf486240c6d4bee2e984841ad385dffb5c896619ba0be12d530c14`.

The TOW2 additions are still uncommitted and unpublished. The latter hash is
staged experimentally but is not yet melee actuator-validated.

# Driver/CAPI pairing

Use the bridge with its matching Toolkit raw-trigger driver/CAPI package.
PlayStation VR2 App updates may restore Sony's driver. Install or replace the
raw-trigger driver only while SteamVR is closed, retain the generated backup,
and restart SteamVR before starting the bridge.

Toolkit's upstream README marks the project unofficial, non-commercial and
provided without warranty. Those constraints remain applicable to this
experimental integration.

# Related

- [TOW2 working state](../games/outer-worlds-2.md)
- [TOW2 PSVR2 integration](../fixes/tow2-psvr2-per-gun-haptics.md)
- [CyberpunkVR Port reference](cyberpunk-vr-port.md)

# Citations

[1] [PSVR2Toolkit community fork](https://github.com/satyaloka93/PSVR2Toolkit/tree/cyberpunk-dsx-bridge)
[2] [Toolkit bridge source](https://github.com/satyaloka93/PSVR2Toolkit/tree/cyberpunk-dsx-bridge/projects/psvr2_toolkit_dsx_bridge)
[3] [Original PSVR2Toolkit project](https://github.com/BnuuySolutions/PSVR2Toolkit)

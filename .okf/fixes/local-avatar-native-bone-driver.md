---
type: fix
title: Local-avatar resolution and native bone driver (UObjectHook)
description: Resolve the real visible player avatar actor/mesh instead of trusting AcknowledgedPawn, filter out menu/inventory proxy contexts, and optionally drive hand bones directly via a validated native bone driver. get_local_pawn falls back to the resolver.
tags:
- uobjecthook
- avatar
- bones
- avowed
timestamp: '2026-07-20T00:00:00Z'
resource: src/mods/UObjectHook.cpp
---

# Problem

Modern UE5 titles (patched Avowed) decouple the visible local avatar from the
simple `AcknowledgedPawn` assumption. UEVR would target the wrong actor (or a
menu/inventory proxy), breaking hand/weapon attachment.

# Fix components

In `src/mods/UObjectHook.{cpp,hpp}`:

- `resolve_local_avatar_actor()` / `resolve_local_avatar_mesh()` with caching,
  heuristics for the primary skeletal mesh, and **menu/proxy-context filtering**
  (gameplay avatar vs inventory preview).
- Safer UObject handling: `is_probably_valid_uobject_ptr(...)`, validated scene
  component transform access — reduces stale-pointer crashes during load
  transitions.
- **Native bone driver** (config `UObjectHook_LocalAvatarNativeBoneDriver`):
  late-stage animation hook, transform-array layout validation, raw
  component-space writes to resolved left/right hand bones, socket-name
  fallback, and runtime gating/blocking when layout validation fails.
  Architecture lesson: a global ProcessEvent hook for this was abandoned —
  a startup-only targeted native hook plus game-thread apply is the survivable
  design.
- `PluginLoader::get_local_pawn` falls back to
  `UObjectHook::resolve_local_avatar_actor(index)` when `AcknowledgedPawn`
  is missing — plugins and Lua scripts get the right actor for free.
- Lua gained `get_grip_pose` / `get_aim_pose` / `get_grip_transform` /
  `get_aim_transform` (`lua-api/lib/src/ScriptContext.cpp`) for pose-anchored
  attach logic.

# Current stance

Implemented and important, but the live Avowed profile still runs conservative
fallback attach (`NATIVE_BONE_DRIVER_OWNS_WEAPONS=0`,
`UObjectHook_LocalAvatarNativeBoneDriver=false`) — see
[/games/avowed.md](/games/avowed.md). Treat native ownership as the long-term
path, not the default.

# Citations

- `AVOWED_UEVR_UPDATE.md` §1.6–1.7, §1.15–1.16
- `AVOWED_DEBUG_LOG.md` — native bone-driver pivot sections

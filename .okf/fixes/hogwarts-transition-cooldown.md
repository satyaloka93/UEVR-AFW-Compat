---
type: fix
title: Hogwarts-only native-stereo post-transition cooldown
description: After a load/world transition, wait an extra cooldown window (~6s) before native stereo may re-enable, on top of the stable-frame safe point; executable-scoped to Hogwarts Legacy. Stopped the TaskGraph access-violation crashes.
tags:
- hogwarts-legacy
- native-stereo
- crash
- cooldown
timestamp: '2026-07-20T00:00:00Z'
resource: src/mods/vr/FFakeStereoRenderingHook.cpp
---

# Problem

Hogwarts Legacy repeatedly cycled `Load transition detected; enabling
passthrough` → `Safe point reached after 60 stable frames; enabling native
stereo fix`, re-arming the fragile native-stereo path on every transition and
eventually crashing (`EXCEPTION_ACCESS_VIOLATION` on a TaskGraph thread).
Conservative config alone did not fix it.

# Fix

In `FFakeStereoRenderingHook.{cpp,hpp}`, gated by
`is_hogwarts_legacy_executable()`:

- Record the last native-stereo transition time when entering load-stall or
  world-transition passthrough.
- Enforce a post-transition **cooldown** before native stereo can re-enable
  (log: `Waiting 6s post-transition cooldown before re-enabling native stereo
  fix`); the existing stable-frame safe point still applies afterwards.
- Reset/log cooldown state in native-stereo runtime reset paths.

Validated: no-crash runs with the cooldown active, `wandpos.dll` restored, and
no `Falling back to direct pose enqueue` lines (that fallback is rejected —
see [/games/hogwarts-legacy.md](../games/hogwarts-legacy.md)).

# Reuse guidance

If a game's log shows rapid passthrough→re-enable cycling around loads and it
crashes later on a game thread, add (or generalize) this cooldown before trying
anything more invasive. Builds on
[/fixes/native-stereo-safe-activation.md](native-stereo-safe-activation.md).

# Citations

- `Hogwarts_Project.md`

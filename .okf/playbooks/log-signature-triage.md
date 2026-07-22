---
type: playbook
title: UEVR log-signature triage
description: Known log.txt signatures mapped to failure domains and the fix/playbook to reach for — read the log before the dump, and rule out Lua first.
tags:
- logging
- triage
- debugging
timestamp: '2026-07-20T00:00:00Z'
---

# First pass: rule Lua in or out

Search `%APPDATA%\UnrealVRMod\<Game>\log.txt` for `sol:`, `traceback`, syntax
errors, script load failure. Absent → it's framework/engine-side, not the
script (this redirected the whole Hogwarts investigation on day one).

# Signature table

| Log signature | Meaning | Go to |
|---|---|---|
| Fake-stereo vtable/constructor scan failure | game patch moved the reference | [/fixes/stereo-vtable-scan-widening.md](../fixes/stereo-vtable-scan-widening.md) |
| Repeated `Previous instruction does not use the same register as the dereference` + null-deref handler activity in FFakeStereoRenderingHook | native-stereo path unstable, pre-crash state | [/fixes/native-stereo-safe-activation.md](../fixes/native-stereo-safe-activation.md) |
| `Load transition detected; enabling passthrough` → `Safe point reached...` cycling rapidly | transition churn re-arming native stereo | [/fixes/hogwarts-transition-cooldown.md](../fixes/hogwarts-transition-cooldown.md) |
| `XR_ERROR_TIME_INVALID` / `XR_FRAME_DISCARDED` / `xrEndFrame failed` loop | stale predicted display time | [/fixes/openxr-authoritative-wait-frame.md](../fixes/openxr-authoritative-wait-frame.md) |
| Present heartbeat stops while game-thread logs continue | Present/GPU-path **stall**, not a crash | [/fixes/tow2-backbuffer-fallback-openxr-prep.md](../fixes/tow2-backbuffer-fallback-openxr-prep.md), [/fixes/tow2-cvar-scanner-bypass.md](../fixes/tow2-cvar-scanner-bypass.md) |
| Title splash then indefinite "waiting" | Ambiguous in TOW2: CVar scan, incomplete view-extension discovery, or GameThread UObject traversal | Check for `Found final frame count`; then [/fixes/tow2-cvar-scanner-bypass.md](../fixes/tow2-cvar-scanner-bypass.md), [/fixes/tow2-view-extension-analyzer-threshold.md](../fixes/tow2-view-extension-analyzer-threshold.md), and capture [/playbooks/in-process-hang-dump.md](in-process-hang-dump.md) |
| A2/A3 analyzer samples stop without `Found final frame count` | Fake-stereo analyzer lost its title-transition timing race | [/fixes/tow2-view-extension-analyzer-threshold.md](../fixes/tow2-view-extension-analyzer-threshold.md) |
| `Skipping AddObject call with no validated FUObjectArray-backed candidate` | Guard rejected a changing/unsafe TOW2 AddObject argument; expected protection | [/fixes/tow2-addobject-candidate-guard.md](../fixes/tow2-addobject-candidate-guard.md) |
| Repeated late-session D3D12 rehook attempts | rehook churn tearing down live swapchain | rehook suppression in [/fixes/tow2-backbuffer-fallback-openxr-prep.md](../fixes/tow2-backbuffer-fallback-openxr-prep.md) |
| UE5.7/SHf loops on `FRHITexture2D vtable is null`, `Failed to get back buffer`, `frame_began`, or UI creation | Scene/UI bootstrap never reached a stable owned target; OpenXR may have opened before a valid submit | [/fixes/shf-ue57-openxr-bootstrap.md](../fixes/shf-ue57-openxr-bootstrap.md) |
| `Prevented corruption of memory` (UserScriptExpress) | third-party reflected scanner poking bad addresses | disable the plugin during diagnosis |
| `Falling back to direct pose enqueue` / `RHI command vtable hooking disabled` | stereo-breaking degrade engaged (right eye will be wrong) | rejected path — see [/games/hogwarts-legacy.md](../games/hogwarts-legacy.md) |
| `Command still hooked on a later frame (age=N); forcing immediate reset` | non-fatal command-hook instability | tolerated; monitor |
| `Could not load openxr_loader.dll` | loader missing next to game exe; stays flat | copy loader to `Binaries\Win64` |
| Per-eye shadow/background mismatch (visual, not logged) | Virtual Shadow Maps | [/fixes/virtual-shadow-maps-stereo-mismatch.md](../fixes/virtual-shadow-maps-stereo-mismatch.md) |
| AFW weapon/moving-object trails despite stable eye ownership | Motion-vector scale/runtime mismatch; Previous Frame alone may only reduce it | [/fixes/afw-beta4-motion-vector-scale.md](../fixes/afw-beta4-motion-vector-scale.md) |
| Avowed crafting/loadout `c0000005` in `UObjectBase::process_event` → `USceneComponent::get_world_location` → `tick_attachments` | Attached component lifetime outlived valid module-backed vtable | [/fixes/avowed-stale-attachment-guard.md](../fixes/avowed-stale-attachment-guard.md) |

# Instrumentation worth adding when the table doesn't match

Cheap, throttled telemetry proved decisive repeatedly: once-per-second Present
heartbeat, OpenXR end-frame result logging, swapchain-slot logging, and
stage-scoped progress lines (e.g. around `EvaluateFrameWarp` /
`EndCommandList` / swapchain acquire-wait-execute). Add telemetry *first*, then
the candidate fix — see
[/playbooks/staged-fix-methodology.md](staged-fix-methodology.md).

Timestamps matter: correlate the last render-thread line against continuing
background-thread lines to prove a stall and locate which thread died.

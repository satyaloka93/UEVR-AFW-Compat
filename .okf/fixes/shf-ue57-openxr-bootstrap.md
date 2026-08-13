---
type: fix
title: Silent Hill f — UE5.7 OpenXR and render-target bootstrap
description: 'Historical Joey-lineage SHf injection foundation: staged OpenXR startup plus bounded, UEVR-owned scene/UI resources. Alpha.2 published the Native-era candidate; the current rebuilt profile and SceneView guard are tracked separately.'
tags:
- silent-hill-f
- shf
- ue5.7
- openxr
- d3d12
- startup
- render-target
- native-stereo
timestamp: '2026-08-08T19:35:00+09:00'
---

# Scope

This fix documents the working **Joey Hodge baseline UEVR lineage** and the
bounded resource rules validated by the PureDark beta.4 port. Source commit
`cc0c43f9` publishes the candidate that injects and renders both eyes in Native
Stereo; alpha.2 packages that historical backend/profile era. Alpha.1 remains
unsupported for SHf.

This concept does not define the current profile mode. Crash metadata identifies
the game as UE 5.4.2; “UE5.7” is retained here only as the historical branch
label. The later rebuilt first-person profile starts cold Previous Frame AFW
with Native Stereo Fix off, and its current SceneView protection is documented
in [the fail-closed guard](shf-sceneviewfamily-fail-closed.md).

# Problem

Silent Hill f (`SHf-Win64-Shipping.exe`) did not fail because of one missing
signature. Its UE5.7 startup violated several assumptions in baseline UEVR at
once:

- Slate/UI and view-extension discovery ran before all required engine state was
  stable and could repeatedly create or lose targets;
- LocalPlayer/PostInit and `FViewportInfo` probing could follow unsafe UE5.7
  layouts;
- the normal fake-stereo scene target and `FRHITexture2D` vtable were not
  available early enough;
- OpenXR could reach READY with partial startup tracking state, open a frame,
  and wedge before a valid D3D12 submit existed;
- engine render-target pointers changed while UEVR retained descriptors and
  references to the old external resource;
- repeated CVar polling, discovery logging and immediate D3D initialization
  retries amplified startup stalls.

The result appeared as failure to inject, prolonged waiting, repeated renderer
initialization, null render targets, an open OpenXR frame that never advanced,
or a crash before stereo became usable.

# Baseline bring-up sequence

The working path was developed as a sequence of narrow checkpoints rather than
one broad renderer replacement.

| Checkpoint | Durable contribution |
|---|---|
| `2bc867a0` — `Add UE 5.7 working branch updates` | Initial guarded UE5.7 hook, D3D12, OpenXR and UObject compatibility work. |
| `d03f9028` — `Restore working UE 5.7 UI target path` | Preserved an in-flight dedicated UI target rather than resetting it every frame; bounded creation retries and retained an owned reference. |
| `0802451c`, `b9f0af06`, `ab080579`, `af1aa2a7`, `e83e1b2d`, `3618de7b`, `bd4e8e3b` | Validated/cached UE5.7 startup and SceneViewExtension discovery, hardened UI retries, reduced diagnostic overhead, and delayed UI creation until PreRenderViewFamily plus FSceneViewFamily offsets were proven. |
| `80cf1ac4` — `Reduce CVar startup overhead` | Loaded each CVar file once, updated only frozen CVars outside the open UI, and avoided per-frame full-list work. |
| `6e4f0c5e` — `Improve UE 5.7 startup and CVar handling` | Added bounded D3D12 initialization backoff, made unavailable CVar setters self-disable, and added OpenXR/tick-state diagnostics. |
| Parallel checkpoint `57shf54`: `44d613cd` then `f10ec09a` | Applied the SHf OpenXR/viewport correction, then UE5.7 OpenXR resolution reset, frame-sync recovery and input handling. |
| Final `shf57` lineage: `835dbccb` then `c199a185` | Applied substantially the same two corrections in the opposite order before the resource-lifetime fixes; `c199a185` adds SHf executable scoping, valid frame-loop-state startup, first-frame synchronization, validated FSceneViewport separate-RT bytes, and fail-soft UE5.7 viewport/LocalPlayer handling. |
| `7bbbbf3a` — `Reuse stable SHf UI texture` | Reused a dimension/format-compatible UI target instead of repeatedly rebuilding the renderer. |
| `b8bb816e`, tag `shf57` — `Stabilize SHf volatile scene copy` | Replaced retained references to the volatile external scene RT with a UEVR-owned committed-resource copy and closed empty startup frames while waiting for valid poses. |

The current baseline integration tree retains additional SHf recovery work that
was not isolated into a clean historical commit: scene-capture-driven
`FRHITexture2D` vtable bootstrap, an owned stable scene-capture copy,
real-backbuffer plus scene-capture composition, D3D rehook suppression, and
direct RHI pose enqueue. These mechanisms are required knowledge, but they must
not be represented as contents of tag `shf57`; preserving their exact source
state remains a checkpoint gap.

Later commits `258f5983` and `529d1644` handled SHf's mono cinematic as a
backend-controlled 2D transition. They improve playability but are not required
to understand the original injection/bootstrap failure.

# Local PureDark beta.4 validation

The first attempted port was unsafe: it created 1,289 UI textures and performed
1,290 D3D12 texture setups in roughly 52 seconds, consumed nearly all VRAM,
flattened textures and left the Native right eye black. It was reverted rather
than tuned in place.

The runtime-tested replacement added:

- stable UI texture reuse with a four-attempt per-resolution circuit breaker;
- 500 ms throttling between D3D setup retries;
- owned stable scene and scene-capture copies;
- explicit right-eye composition fallback;
- direct SHf RHI pose enqueue;
- redundant D3D rehook suppression while the Windows message hook remains
  intact.

A validated Native run produced one UI allocation, 327 reuse reports, five
bounded D3D setup passes, no circuit-breaker activation and no backbuffer-fail
loop. Both eyes rendered and AFW could subsequently be selected manually
without the earlier VRAM runaway. These results validate the injection/resource
fix, not AFW image quality or publication readiness.

For the historical alpha.2 Native profile, validation used a fresh process,
early injection and `VR_RenderingMethod=0`. Do not apply that startup state to
the later rebuilt profile, which has separately validated cold-start Previous
Frame AFW. Keep warning-level logging during performance tests; high-frequency
texture diagnostics can dominate the log and add avoidable I/O overhead.

# Key implementation rules

## Validate and cache discovery

UE5.7 SceneViewExtension indices and frame-count offsets are cached per
executable only after validation. A cached entry is rejected if indices are out
of range, functions are null/outside the expected module, or the frame-count
offset is implausible. This turns later launches into a validated fast path
without making stale offsets authoritative.

Dedicated UI creation is not scheduled merely because a Slate call occurred.
It waits until PreRenderViewFamily has been observed and FSceneViewFamily
offsets are ready, tracks a creation generation, owns the resulting texture
reference, and times out/retries instead of accumulating in-flight UObjects.

## Fail soft on unknown UE5.7 layouts

- If PostInitProperties cannot be identified, skip the LocalPlayer bootstrap
  rather than dispatch through a guessed virtual function.
- Do not use the old `FViewportInfo` render-target-provider probing path on
  UE5.7 without a validated texture.
- Adopt Slate or engine textures only after pointer, vtable, native D3D12
  resource and descriptor validation.
- Before forcing SHf's separate scene RT, validate the FSceneViewport vtable and
  require the candidate bool bytes to be exactly `0` or `1`.

## Keep OpenXR frame ownership recoverable

`can_run_frame_loop()` treats READY, SYNCHRONIZED, VISIBLE and FOCUSED as valid
frame-loop states once the session is ready. SHf startup also needed:

- first-frame synchronization on the READY transition;
- acceptance of startup poses when position/orientation are valid and some
  tracked state exists, instead of requiring every tracked bit immediately;
- rejection of non-finite or sign-invalid eye projections;
- empty `xrEndFrame` closure when a pre-submit frame opened before valid poses;
- recovery from a stuck `frame_began`/`XR_ERROR_CALL_ORDER_INVALID` sequence;
- submit only when `begin_frame` actually leaves a frame open.

These guards prevent an early partial pose or missing render target from
poisoning all subsequent waits and submits.

## Own resources whose engine identity is volatile

SHf exposes an external scene RT whose pointer can change while dimensions and
format remain stable. UEVR must not attach long-lived SRV/RTV state directly to
that object. Tag `shf57` creates `SHf Stable Scene Copy`, rebuilds it only when
the source descriptor changes, copies from the engine resource using its
observed shader-resource state, and suppresses unsafe mirror/2D use until the
owned copy exists.

The retained follow-up applies the same ownership rule to scene capture with
`SHf Stable Scene Capture Copy`, then composites the real backbuffer plus stable
scene capture into the OpenXR double-wide target. When SHf does not expose the
normal `FRHITexture2D` vtable early enough, it allows scene capture to bootstrap
on the render thread and publishes the vtable from the resulting valid
FRenderTarget. It also uses direct RHI pose enqueue instead of the fragile
generic RHI command hook. These follow-up mechanisms are in the current
integration source, not in tag `shf57`.

# Healthy signatures

A healthy baseline launch should progress rather than loop indefinitely:

```text
[OpenXR] Session is READY; synchronizing first frame
[OpenXR] Got first valid poses ...
Found final frame count offset ...
Done setting up BeginRenderViewFamily hook!
[SHf] Scene capture texture created via render-thread bootstrap!
[SHf][D3D12] Creating owned stable scene copy ...
[SHf] Reusing stable UI texture ...
```

Initial `Failed to get back buffer (D3D12)` retries can occur before the scene
target exists; they are only healthy if they stop after bootstrap. Repeated
`FRHITexture2D vtable is null`, perpetual `frame_began`, recurring D3D rehooks,
or continuous renderer setup means the working path was not reached.

# Scope and porting rule

This remains primarily a Joey-derived injection and resource-lifetime fix. The
published PureDark beta.4 port proves those pieces can coexist with AFW, but
rendering-mode guidance belongs to the current game/profile concept. Port
startup, OpenXR ownership and owned-resource pieces narrowly; do not copy the
original diagnostic/RenderInspector subsystem wholesale. Preserve exact UESDK
compatibility and treat PDAFW runtime, header and callers as one checkpoint.

The DLSS startup-quality workaround and AFW visual/performance limits are
tracked in [the SHf game profile](../games/silent-hill-f.md), not generalized as
part of this injection fix.

# Related

- [/games/silent-hill-f.md](../games/silent-hill-f.md)
- [/fixes/native-stereo-safe-activation.md](native-stereo-safe-activation.md)
- [/fixes/render-target-validation-hardening.md](render-target-validation-hardening.md)
- [SHf SceneView fail-closed guard](shf-sceneviewfamily-fail-closed.md)
- [/playbooks/checkpoint-and-recovery.md](../playbooks/checkpoint-and-recovery.md)

# Citations

- Git lineage: `2bc867a0`, `d03f9028`, `0802451c`, `b9f0af06`,
  `ab080579`, `af1aa2a7`, `e83e1b2d`, `3618de7b`, `bd4e8e3b`,
  `80cf1ac4`, `6e4f0c5e`; parallel SHf checkpoints `44d613cd` →
  `f10ec09a` and `835dbccb` → `c199a185`; final lineage `7bbbbf3a`,
  `b8bb816e`, `258f5983`, `529d1644`
- Tags: `57shf54`, `shf57`
- Published source: commit `cc0c43f9`
- Runtime evidence placeholders:
  `<evidence-root>/shf-afw-bootstrap-candidate-20260720` and
  `<evidence-root>/shf-stable-resources-candidate2-20260722`
- Code: `src/Framework.cpp`, `src/mods/VR.cpp`,
  `src/mods/vr/CVarManager.cpp`, `src/mods/vr/D3D12Component.cpp`,
  `src/mods/vr/FFakeStereoRenderingHook.cpp`,
  `src/mods/vr/runtimes/OpenXR.cpp`

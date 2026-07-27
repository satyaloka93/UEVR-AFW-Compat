---
type: fix
title: Silent Hill 2 (SHProto) — unified backend with forced Native cold-start
description: The shipped AFW-Compat branch restores SH2 by pinning baseline UESDK 491f973a and forcing Native before OpenXR swapchain initialization; a runtime Native-to-AFW switch has been observed on the matching deployed checkpoint, but broader AFW safety remains under validation.
tags:
- silent-hill-2
- sh2
- shproto
- afw
- native-stereo
- uesdk
- swapchain
- unification
timestamp: '2026-07-27T09:30:00+09:00'
---

# Scope

Maintain one `afw-beta4-game-compat` source branch for SH2
(`SHProto-Win64-Shipping.exe`), Avowed and TOW2. The shipped branch tip is
`a3d3128c`; its executable changes are:

- `e0f7c5c7` — pin UESDK `491f973a` and restore baseline `FUObjectItem::object` callers;
- `75c172c5` — force SH2 to Native immediately before OpenXR swapchain initialization.

See the [SH2 game state](../games/silent-hill-2.md), the
[AFW integration project](../projects/puredark-afw-integration.md), and the
[narrow-port decision](../decisions/narrow-port-scope.md).

# Symptoms addressed

Two distinct failures had to remain separate:

1. Builds using UESDK `9034a857` produced severe SH2 regressions in this fork,
   including unusable rendering and GPU-hang/TDR observations during the wider
   beta.4 investigation.
2. After returning to the baseline UESDK, a profile saved in AFW could still
   enter a broken cold-start state: the desktop menu rendered while OpenXR had
   no valid double-wide swapchain and repeatedly reported `Double wide
   swapchain not created`.

The first Native-force attempt ran too late. It changed the rendering-method
value after swapchain setup and left a hybrid state: Native value with AFW/AFR
swapchain ownership.

# Correction

## 1. Pin the tested UESDK for the unified SH2/Avowed/TOW2 scope

Commit `e0f7c5c7` changes the submodule from `9034a857` to baseline
`491f973a` and updates the three dependent callers from
`FUObjectItem::get_object()` to the baseline `FUObjectItem::object` field.
The beta.4 `PDAFWPlugin.h` ABI remains unchanged.

The runtime A/B establishes the UESDK revision as part of the failing/working
checkpoint. It does **not** by itself prove the exact low-level corruption
mechanism; no matching source-level trace isolated a particular UE layout gate.
Do not generalize this pin to SHf: alpha.2's SHf/UE5.7 candidate used
`9034a857`, so SHf must be revalidated separately on the unified branch.

## 2. Force Native before swapchain initialization

Commit `75c172c5` detects `shproto-win64-shipping.exe` in
`VR::on_config_load()`. Immediately after the ModValue load loop and before
`initialize_openxr_swapchains()`, it changes any loaded rendering method to
`NATIVE_STEREO` and emits:

```text
[SH2] Forcing Native Stereo before swapchain init (AFW cold-start unsupported); switch to AFW at runtime if desired
```

The value is intentionally forced in memory rather than rewriting the profile.
A profile may therefore remain saved as `VR_RenderingMethod=3`, while every new
process still creates Native swapchains first. Runtime Native-to-AFW switching
remains available. Restart the game to leave AFW; PDAFW has no teardown API.

# Validation checkpoint

The 2026-07-27 deployed checkpoint was reported working across repeated SH2
launches and a runtime AFW switch:

| Artifact | SHA-256 |
|---|---|
| `UEVRBackend.dll` | `eec89cd4e000ab83c0392caac59e73588125b71b8e054731f6472e7b7b08efbe` |
| `UEVRBackend.pdb` | `846fac27028bebf86e3e5abab5a5248bb1f8b57ee6b1f8465202d046c34bcb54` |
| deployed `PDAFWPlugin.dll` | `b129118ba239e0c9fd7b0c803dab0199242af7142c7b9541e656e2f3eaca8ff9` |
| `openxr_loader.dll` | `3bb4eb190024f92eecf4a0bf33669b5d0b27571ddf4c36a33c58de199ee536c1` |

The deployed PDAFW hash is the amended beta.4-derived runtime, not the official
beta.4 hash `76bbc4d7...`.

The saved profile at validation time used Combined mode (`VR_AFW_FramewarpMode=3`),
Ghosting Fix enabled, moving-object brightness correction disabled, and
`VR_NativeStereoFix=false`. The startup log showed the force before swapchain
bring-up and one recoverable `XR_ERROR_TIME_INVALID` / `XR_FRAME_DISCARDED`
pair. Sustained AFW gameplay and driver safety remain required before treating
SH2 AFW as generally safe.

# Superseded attempts

Earlier local commits added SH2 to a real-backbuffer fallback and an
`FRHITexture2D` bootstrap path while the late-force hybrid state still existed.
Once the force moved before swapchain initialization, repeated tests no longer
needed either change. They were dropped from the published three-commit line;
retain their hashes only as diagnostic history, not as part of the fix.

# Safety interpretation

- This checkpoint supersedes the old claim that SH2 must always use the normal
  non-AFW backend, but only for the exact shipped source/runtime pairing above.
- It does **not** erase earlier official beta.4/hotfix GPU hangs or the NVIDIA
  bugcheck produced by other backend/PDAFW combinations.
- Do not infer that official beta.4 AFW cold-start is repaired. The fix avoids
  AFW cold-start and permits a later runtime switch.
- Native-to-AFW is the tested direction; AFW-to-Native still requires restart.

# Citations

- Shipped branch: `origin/afw-beta4-game-compat` at `a3d3128c`.
- Executable commits: `e0f7c5c7`, `75c172c5`.
- Historical divergence commits: `c1528b7c`, `276300db`.
- Deployed log signature: `[SH2] Forcing Native Stereo before swapchain init`.

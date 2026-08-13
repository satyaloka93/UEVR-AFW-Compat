---
type: fix
title: Silent Hill 2 (SHProto) — unified backend with forced Native cold-start
description: Force SH2 to Native before OpenXR swapchain initialization. The historical unified checkpoint established the startup fix; current policy remains Native-only because later AFW runs produced device hangs and an NVIDIA bugcheck.
tags:
- silent-hill-2
- sh2
- shproto
- afw
- native-stereo
- uesdk
- swapchain
- unification
timestamp: '2026-08-08T19:35:00+09:00'
---

# Scope

Maintain one `afw-beta4-game-compat` source branch for SH2
(`SHProto-Win64-Shipping.exe`), Avowed and TOW2. The startup correction
originated in the shipped line ending at `a3d3128c` and remains in the current
public descendant `70f6e508`:

- `e0f7c5c7` — historical UESDK `491f973a` checkpoint and baseline
  `FUObjectItem::object` callers;
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
A stale profile can therefore still say `VR_RenderingMethod=3`, while every new
process creates Native swapchains first. The maintained profile should now save
`VR_RenderingMethod=0` explicitly. Remain Native for the entire process; later
AFW evidence makes runtime switching unsupported.

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

The saved profile at that historical validation time used Combined mode
(`VR_AFW_FramewarpMode=3`), Ghosting Fix enabled, moving-object brightness
correction disabled, and `VR_NativeStereoFix=false`. The startup log showed the
force before swapchain bring-up and one recoverable `XR_ERROR_TIME_INVALID` /
`XR_FRAME_DISCARDED` pair. That profile pairing is superseded: the current
known-good SH2 profile uses Native with `VR_NativeStereoFix=true` and does not
activate AFW.

# Superseded attempts

Earlier local commits added SH2 to a real-backbuffer fallback and an
`FRHITexture2D` bootstrap path while the late-force hybrid state still existed.
Once the force moved before swapchain initialization, repeated tests no longer
needed either change. They were dropped from the published three-commit line;
retain their hashes only as diagnostic history, not as part of the fix.

# Safety interpretation

- The startup force remains valid and keeps swapchain creation Native.
- It does **not** erase official beta.4/hotfix GPU hangs or the NVIDIA bugcheck.
- Current policy is SH2 Native-only with Native Stereo Fix on.
- Do not infer that AFW cold-start or runtime AFW is repaired; no AFW candidate
  is approved.

# Citations

- Originating shipped checkpoint: `a3d3128c`; current public descendant: `70f6e508`.
- Executable commits: `e0f7c5c7`, `75c172c5`.
- Historical divergence commits: `c1528b7c`, `276300db`.
- Deployed log signature: `[SH2] Forcing Native Stereo before swapchain init`.

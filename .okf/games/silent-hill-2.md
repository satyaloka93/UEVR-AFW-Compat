---
type: game-profile
title: Silent Hill 2 (SHProto) — forced-Native compatibility checkpoint
description: 'SH2 remains Native-only: force Native before OpenXR swapchain initialization, keep Native Stereo Fix on for the current profile, and preserve prior AFW device-hung and NVIDIA bugcheck evidence.'
tags:
- silent-hill-2
- sh2
- shproto
- ue5
- openxr
- d3d12
- native-stereo
- afw
- unification
- 6dof
- ik
- profile
timestamp: '2026-08-08T19:35:00+09:00'
---

# Identity

- Executable: `SHProto-Win64-Shipping.exe`.
- Game: Silent Hill 2 remake, codename SHProto.
- Renderer/runtime: D3D12 + OpenXR.
- Distinct from [Silent Hill f](silent-hill-f.md), whose executable, UE5.7
  bootstrap and UESDK requirements are different.

# Current working checkpoint

The forced-Native correction originated in the shipped line ending at
`a3d3128c` and remains in the current public descendant at `70f6e508`:

1. `e0f7c5c7` established the historical UESDK `491f973a` checkpoint and
   restored baseline `FUObjectItem::object` access;
2. `75c172c5` forces SH2 to Native before OpenXR swapchain initialization;
3. later public commits preserve that executable-scoped startup force while
   adding other games' compatibility work.

The [forced Native cold-start fix](../fixes/sh2-shproto-afw-cold-start-native-force.md)
was confirmed across repeated launches. Although one historical
Native-to-AFW switch completed on the exact amended-runtime checkpoint, later
AFW runs produced device hangs and an NVIDIA bugcheck. Current policy is
Native-only; do not treat the saved frame-warp mode as permission to activate
AFW.

## Deployed artifacts

| Artifact | SHA-256 |
|---|---|
| `UEVRBackend.dll` | `eec89cd4e000ab83c0392caac59e73588125b71b8e054731f6472e7b7b08efbe` |
| `UEVRBackend.pdb` | `846fac27028bebf86e3e5abab5a5248bb1f8b57ee6b1f8465202d046c34bcb54` |
| amended beta.4-derived `PDAFWPlugin.dll` | `b129118ba239e0c9fd7b0c803dab0199242af7142c7b9541e656e2f3eaca8ff9` |
| `openxr_loader.dll` | `3bb4eb190024f92eecf4a0bf33669b5d0b27571ddf4c36a33c58de199ee536c1` |

This is a source-branch/deployment checkpoint, not a tagged binary release.
The backend does not expose a useful embedded commit string, so identify it by
hash together with its PDB and PDAFW runtime.

## 2026-07-31 revalidation on the migrated VS toolchain

A Visual Studio auto-update removed the original toolchain (MSVC 14.37, Windows
SDK 10.0.22000); builds now use MSVC 14.44 / SDK 10.0.26100 after a full
`build/` wipe and reconfigure. The same shipped source rebuilt on the new
toolchain was confirmed working for SH2 launch:

| Artifact | SHA-256 |
|---|---|
| `UEVRBackend.dll` (new toolchain) | `b72b72ae52329f6267e6d3670e9081fdfa3fde76ed2cb33763c02b633b23e1b5` |
| `UEVRBackend.pdb` (new toolchain) | `abb22154061a1d96314f8f7d66434aa50d27e17f9ea250e15e6286c4a92f0d1e` |

The original 14.37-built pair is preserved beside it as
`UEVRBackend.shipped-unified.dll`/`.pdb`.

The same session also re-ran the UESDK A/B empirically: branch `shf-uesdk-ab`
(shipped tip + revert of `e0f7c5c7`, i.e. hardened UESDK `9034a857` restored,
nothing else changed) froze SH2 before world load — repeating
`XR_ERROR_TIME_INVALID`/`XR_FRAME_DISCARDED` with growing display-time deltas
and "vtable is already hooked" warnings. This re-confirms the baseline
`491f973a` pin as a checkpoint requirement for SH2 even though the hardened
SDK's new `FUObjectItem` layout code is version-gated to UE5.7+ executables;
the mechanism by which it breaks SH2 remains unproven. The A/B also showed the
UESDK pin is *not* the cause of the concurrent
[SHf regression](silent-hill-f.md) (identical SHf crash on both revisions).

# Operating procedure

1. Start a fresh SH2 process and inject once the game can render a real 3D
   frame. Early stereo-device discovery may fail twice and then recover.
2. Use `VR_RenderingMethod=0` and keep
   `VR_NativeStereoFix=true` for the current known-good profile. The backend's
   startup force remains a safety net before swapchain creation.
3. Confirm the log contains:

   ```text
   [SH2] Forcing Native Stereo before swapchain init
   ```

4. Remain in Native for the entire process. Do not activate AFW unless a new,
   explicit AFW investigation is requested.

# Validated profile details for the current policy

- Cold-start and runtime method: Native (`VR_RenderingMethod=0`).
- `VR_NativeStereoFix=true` for the current known-good profile.
- `VR_AFW_FixMovingObjectBrightnessFlickering=false` remains the global safety
  setting even though AFW is not activated.
- Warning-level logging remains recommended for regression comparisons.

Native Stereo Fix behavior is backend/profile-specific. Preserve this exact
SH2 pairing; do not copy its value to SHf or TOW2.

# First-person and 6DoF profile layer

SH2's renderer checkpoint and its first-person/6DoF profile are separate
systems. A working full profile uses a coordinated game plugin plus Lua stack:

- `plugins/sh2r.dll` for game-specific integration;
- `camera.lua` for first-person camera ownership;
- `main.lua` to initialize pawn, attachments, input, IK, hands and interaction;
- `melee.lua` for weapon-hand combat behavior;
- profile data for per-weapon attachment, hand, IK, UI and input calibration.

That full profile intentionally has `UObjectHook_EnabledAtStartup=false`; it is
not a saved native-UObjectHook attachment. A camera-only profile variant also
exists and is useful as a minimal isolation control. Preserve these variants
instead of merging files between them: the full profile's plugin, scripts and
data form an atomic compatibility set.

The reusable lesson differs from both current examples in the
[stable 6DoF playbook](../playbooks/basic-6dof-setup.md): TOW2 needed a narrowly
validated backend enrollment exception for one late mesh, while Avowed needed a
runtime-avatar resolver. SH2 already justifies a full plugin/IK architecture
because camera, hands, two-hand interaction and melee are coupled features.
Do not enable UObjectHook or graft TOW2's enrollment path onto SH2 merely to
make its configuration resemble another game.

6DoF profile success does not relax renderer rules. Validate it only in the
known Native state; AFW remains out of scope for the maintained SH2 profile.

# Lineage and prior evidence

- The normal non-AFW baseline remained the historical safe fallback:
  commit `6f66affc`, backend
  `fb91386c02a27b53cb7d4db78fd807a3e1165947c9667331e3b5141a8369855b`.
- PureDark beta3.1 / Joey-Merged `021d45b7` was qualitatively smooth in Native
  and AFW, although no final quantitative capture was saved.
- Earlier PureDark beta.4-derived backends produced a severe Native regression
  even when PDAFW was not loaded, while official beta.4, the SH2 hotfix and
  other tested PDAFW combinations produced `DXGI_ERROR_DEVICE_HUNG` in AFW.
- One Combined transition on an earlier checkpoint triggered NVIDIA bugcheck
  `0x139` (`nvlddmkm.sys`). Those results remain valid for those exact binaries
  and are not erased by the new checkpoint.

Do not restore the old conclusion that “PureDark plain beta.4 works Native or
AFW” as a general statement. The maintained path is forced Native startup plus
the current SH2 profile's Native Stereo Fix.

# Current limitations

- SH2 AFW remains unsafe due `DXGI_ERROR_DEVICE_HUNG` and the prior NVIDIA
  `0x139` bugcheck; no AFW candidate is approved.
- One historical startup log contained a recoverable
  `XR_ERROR_TIME_INVALID` / `XR_FRAME_DISCARDED` pair.
- The current combined f37 plus SHf-guard backend still requires a Native-only
  SH2 regression pass before publication.
- SHf and TOW2 have separate profile/runtime rules; do not generalize SH2's
  Native Stereo Fix or Native-only policy to them.

# Relationships

- Fix: [SH2 forced Native cold-start](../fixes/sh2-shproto-afw-cold-start-native-force.md)
- Project: [PureDark AFW integration](../projects/puredark-afw-integration.md)
- Decision: [Narrow port scope](../decisions/narrow-port-scope.md)
- Profile comparison: [SH2 AFW/Native profile conversion](../playbooks/afw-profile-conversion.md)
- 6DoF method: [Stable 6DoF profile creation](../playbooks/basic-6dof-setup.md)
- Contrast: [Silent Hill f](silent-hill-f.md)

---
type: game-profile
title: Silent Hill 2 (SHProto) — Native-first unified AFW checkpoint
description: SH2 now starts reliably on the shipped unified branch by pinning baseline UESDK 491f973a and forcing Native before OpenXR swapchain initialization; runtime AFW switching is promising on the exact amended-runtime checkpoint but remains narrower than a general SH2 AFW safety claim.
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
timestamp: '2026-08-02T18:17:40+09:00'
---

# Identity

- Executable: `SHProto-Win64-Shipping.exe`.
- Game: Silent Hill 2 remake, codename SHProto.
- Renderer/runtime: D3D12 + OpenXR.
- Distinct from [Silent Hill f](silent-hill-f.md), whose executable, UE5.7
  bootstrap and UESDK requirements are different.

# Current working checkpoint

The shipped source branch is `origin/afw-beta4-game-compat` at `a3d3128c`:

1. `e0f7c5c7` pins UESDK `491f973a` and restores baseline
   `FUObjectItem::object` access;
2. `75c172c5` forces SH2 to Native before OpenXR swapchain initialization;
3. `a3d3128c` records the initial OKF state.

The [forced Native cold-start fix](../fixes/sh2-shproto-afw-cold-start-native-force.md)
was confirmed across repeated launches. A runtime Native-to-AFW switch was also
reported working on the deployed checkpoint. The saved profile used Combined
mode (`VR_AFW_FramewarpMode=3`), but the code forces Native for each new process.

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
2. Leave startup ownership to the code force. `config.txt` may still say
   `VR_RenderingMethod=3`; the backend changes the in-memory value to Native
   before swapchain creation.
3. Confirm the log contains:

   ```text
   [SH2] Forcing Native Stereo before swapchain init
   ```

4. Validate Native before switching to AFW.
5. If testing AFW, retain `VR_AFW_FixMovingObjectBrightnessFlickering=false`.
6. Restart the game to leave AFW. Do not switch AFW-to-Native in-process because
   PDAFW exposes no teardown API.

# Validated profile details for this checkpoint

- Cold-start method: forced Native (`RenderingMethod=0` in memory).
- Saved frame-warp mode: Combined (`VR_AFW_FramewarpMode=3`).
- `VR_GhostingFix=true` and bootstrap enabled in the tested saved profile.
- `VR_AFW_FixMovingObjectBrightnessFlickering=false`.
- `VR_NativeStereoFix=false` on this unified build; enabling it produced a
  black right eye in this checkpoint.
- Warning-level logging remains recommended for performance comparisons.

The Native Stereo Fix result is backend-specific. Earlier PureDark-derived SH2
diagnostic builds required `VR_NativeStereoFix=true` to control shaking. Do not
copy either value across backend hashes without a fresh-process check.

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

6DoF profile success does not relax renderer rules. Validate it in the known
Native startup state first, and treat any AFW experiment as a separate
fresh-process variable.

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
AFW” as a general statement. The newly working path is the shipped unified
source plus baseline UESDK, forced Native cold-start, and the exact amended
runtime hash above.

# Current limitations

- AFW cold-start is deliberately unsupported; the fix avoids it rather than
  repairing it.
- The latest startup log contained one recoverable
  `XR_ERROR_TIME_INVALID` / `XR_FRAME_DISCARDED` pair.
- Runtime AFW was observed working, but sustained gameplay, transitions,
  moving-object quality and NVIDIA driver safety still need repeated testing.
- TOW2 should be re-confirmed after the UESDK pin change.
- SHf is not covered by this unification claim and must be revalidated because
  its alpha.2 UE5.7 candidate used UESDK `9034a857`.

# Relationships

- Fix: [SH2 forced Native cold-start](../fixes/sh2-shproto-afw-cold-start-native-force.md)
- Project: [PureDark AFW integration](../projects/puredark-afw-integration.md)
- Decision: [Narrow port scope](../decisions/narrow-port-scope.md)
- Profile comparison: [SH2 AFW/Native profile conversion](../playbooks/afw-profile-conversion.md)
- 6DoF method: [Stable 6DoF profile creation](../playbooks/basic-6dof-setup.md)
- Contrast: [Silent Hill f](silent-hill-f.md)

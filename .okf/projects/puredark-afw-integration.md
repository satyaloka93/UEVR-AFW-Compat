---
type: project
title: PureDark AFW integration — ongoing effort state
description: Live state of the narrow AFW compatibility fork; alpha.4 publishes TOW2 explicit enrollment and FMalloc case handling, the SHf fail-closed SceneView guard, Lua shutdown lifetime correction, and maintained SHf first-person/6DoF profile as an experimental prerelease.
tags:
- afw
- puredark
- avowed
- tow2
- shf
- sh2
- ongoing
timestamp: '2026-08-12T13:33:00+09:00'
resource: .
---

# Goal and scope

Run PureDark AFW in games already stabilized on the regular backend without
importing broad lifecycle, renderer, VRS/UI, or artifact-prone behavior. The
standing rules are in [narrow port scope](../decisions/narrow-port-scope.md).

# Branch and preservation state

- Public branch: `afw-beta4-game-compat`, based on PureDark
  `AFW` commit `e260ffe8` (`UEVR_AFW_v1.0-beta.4` tag).
- Public tip `70f6e508` included TOW2 explicit motion-controller enrollment,
  the 6DoF overlay package and cross-linked playbooks.
- Alpha.4 adds backend commit `b64dacc8`, the maintained SHf profile commit
  `fc7dbc52`, and the accompanying canonical documentation/release commit.
- Its UESDK checkpoint is `7610b016`: custom cache baseline `d9ee8a57` plus
  FMalloc case handling (`62471af`) and the small validated SceneView
  offset-publication API (`7610b01`).
- Joey integration is preserved at `021d45b7` plus private stashes/evidence;
  only audited corrections are intended for publication with attribution.
- Experimental deployment remains isolated at `<deploy-dir>`; the primary UEVR
  installation remains untouched.
- Open validation remains explicit. Neither public source nor successful local
  testing promotes a candidate to stable status.

# Runtime and build dependency model

- UESDK remains a gated `PureDark/UESDK` submodule. Builders must link GitHub to
  Epic, obtain repository access, configure SSH, and initialize submodules; its
  source is not copied into the public fork.
- The checked-in `PDAFWPlugin.h` defines the ABI and the dummy implementation
  supplies no-op link-time exports. Real frame warping requires the official
  matching PureDark `PDAFWPlugin.dll`, obtained separately.
- The beta.4 runtime/header/backend callers form one compatibility checkpoint.
  Alpha.2 used official beta.4 runtime hash
  `76bbc4d7a5370ba0d6a1a50de6b81d45223e42b764d2d7c79f0c6c48a59f6b64`.
  The current maintained experimental checkpoint uses the amended beta.4
  runtime hash
  `b129118ba239e0c9fd7b0c803dab0199242af7142c7b9541e656e2f3eaca8ff9`; do
  not interchange these runtimes without matching caller validation.
- See repository file `docs/PDAFW_RUNTIME.md` before building or packaging;
  never mistake the generated dummy DLL for the real runtime.

# Public alpha releases

## Alpha.4

- Prerelease: `afw-beta4-compat-v0.1.0-alpha.4`
- URL: `https://github.com/satyaloka93/UEVR-AFW-Compat/releases/tag/afw-beta4-compat-v0.1.0-alpha.4`
- Tagged source/build commit: `eca65b9b980b4a1bd0d8731d120355d1e69009c9`
- UESDK checkpoint: `7610b0166cdfb6767b67836d11d751bee15900cd`
- Publishes the TOW2 FMalloc case correction, SHf fail-closed SceneView guard,
  Lua exit-time lifetime fix, first released TOW2 explicit-enrollment backend,
  and complete maintained SHf first-person/6DoF profile.
- Published hashes:
  - `UEVRBackend.dll`: `1e502f87bb64484b676dd91c25f52ff74d161c6311ee904bd796e7df2f769593`
  - `UEVRBackend.pdb`: `1d5d7214d351e3a5ebfbcb21d8d15ad743ddbcec0aab2029a1a736fc8e6b8aca`
  - amended beta.4-derived `PDAFWPlugin.dll`: `b129118ba239e0c9fd7b0c803dab0199242af7142c7b9541e656e2f3eaca8ff9`
  - main ZIP: `69f693a92b9ad909725487b2adf1cb8a9f3ba71e6fbbdff6eadd316c6c7ce6bf`
  - symbols ZIP: `3297bda8eab9aad21cffcb158478437284c4d89a2ec739971edcf82bf9a65301`
  - profiles ZIP: `90c542c30dba3d8c273828b7c03c705bb2af033c2de1edb115701c629538c75a`
- Downloaded GitHub assets were rehashed against `RELEASE_SHA256SUMS.txt` after
  publication and all matched.
- Keep alpha.4 marked prerelease until the SHf guard path, clean exit and
  broader maintained-game regression are complete.

## Alpha.2

- Prerelease: `afw-beta4-compat-v0.1.0-alpha.2`
- URL: `https://github.com/satyaloka93/UEVR-AFW-Compat/releases/tag/afw-beta4-compat-v0.1.0-alpha.2`
- SHf backend source commit: `cc0c43f9`
- Adds the rebuilt SHf compatibility backend plus separate sanitized Avowed and
  SHf profile assets. The SHf profile starts Native and contains no rejected
  automatic DLSS reset helper.
- Published hashes:
  - `UEVRBackend.dll`: `41d7eb71c884858e5d016c3d837b16eb394f2dd3a4a0bf6e94d7d371e8c31322`
  - `UEVRBackend.pdb`: `66d18fb66ea451dae093d2304eebf0842afe3ed22dc177b80947db454a9cf686`
  - `PDAFWPlugin.dll`: `76bbc4d7a5370ba0d6a1a50de6b81d45223e42b764d2d7c79f0c6c48a59f6b64`
  - `openvr_api.dll`: `ac164002fa0a24ef7b2cbd2592814389b0c424f8985e5133e48ac79933466b9d`
  - `openxr_loader.dll`: `3bb4eb190024f92eecf4a0bf33669b5d0b27571ddf4c36a33c58de199ee536c1`
  - `UEVRPluginNullifier.dll`: `69e495287d6c6683d69c9f81df67deaa782e35a436aa8abf6022f6ac4ad30462`
  - main ZIP: `c9687a3389f0c5d67603786cfc6d35d926a1cabe98f2d4d30eb585dd63b627a6`
  - symbols ZIP: `8531c856120a8a10ca5e43c68f62f4cf0c4356eba8cb086c29d1c54b693673bf`
  - profiles ZIP: `967acb6e2a3d1a1dd98a70c46fe5039569ed718b4c3dcd58ee1214cffbae1e75`
- Keep alpha.2 marked prerelease until Avowed/TOW2 and sustained SHf gameplay
  are reproduced using the released hashes.

## Alpha.1 historical checkpoint

- Prerelease: `afw-beta4-compat-v0.1.0-alpha.1`
- Source/build commit: `9dfb9746521c5c8d9a317c9d11ed6a7875e1fa6a`
- Alpha.1 remains immutable and unsupported for SHf.
- Published hashes:
  - `UEVRBackend.dll`: `701a5971e88d9d9d0021ab47de5f6d62f9ba31d506f82718fbcb820d8537068a`
  - `UEVRBackend.pdb`: `fea73312d4cee7665d7fecd81cd4880ebad17a27c68300a6ecb40a361d4c9555`
  - main ZIP: `0079f9adf756e044e48481f87ea344a8650561e9f1482f0c59a5b87a02631357`
  - symbols ZIP: `8e2618424b010232c42fbf534c68a079fab7446ac123ff98ec46800474094533`

Both releases use gated UESDK `9034a857`; no UESDK source or compatibility
patch is redistributed. The backend callers and official beta.4 PDAFW runtime
remain one ABI checkpoint.

# Avowed state retained

- Authoritative OpenXR wait-frame timing, Native-only direct RHI pose
  publication, monotonic second pass and 300-byte stereo scan.
- Avowed-only AddObject candidate guard retained on baseline UESDK callers;
  Avowed was reported working after the UESDK pin change, but repeated
  crafting/loadout validation remains required.
- AFW hot work gated to active AFW frames.
- Verified `GetDesiredNumberOfViews` signature and Joey's generation-aware,
  eye-confirmed ghosting state machine with bounded bootstrap.
- Combined Warping/object-motion correction retained; unstable moving-object
  brightness input remains opt-in and normally disabled.
- PSVR2 d-pad/input additions retained.

The first Avowed beta.4 crafting-table run exposed a stale attached-component
virtual dispatch in `UObjectHook::tick_attachments`; dump diagnosis and the
Avowed-scoped guard are documented in
[/fixes/avowed-stale-attachment-guard.md](../fixes/avowed-stale-attachment-guard.md).
One immediate crafting retest completed without a crash. Repeated
crafting/loadout validation is still required before beta.4 is promoted beyond
the experimental install.

# TOW2 injection state

The complete executable-scoped compatibility port now includes D3D12
Present/factory coverage, real-backbuffer fallback, early OpenXR preparation,
validated non-blocking waits, rehook suppression, CVar bypasses and VSM via
`Engine.ini`.

Additional PureDark-branch corrections:

1. XR null-dereference VEH only when Native Stereo Fix is enabled.
2. [TOW2-only analyzer threshold 40](../fixes/tow2-view-extension-analyzer-threshold.md).
3. [Per-call FUObjectArray-backed AddObject validation](../fixes/tow2-addobject-candidate-guard.md), derived from a running-process dump.
4. One-shot [in-process title hang dump](../playbooks/in-process-hang-dump.md)
   temporarily retained because analyzer completion is still timing-sensitive.
5. Published [explicit late-component enrollment](../fixes/tow2-explicit-component-enrollment.md)
   for true Steam weapon 6DoF without broad UObject scanning.
6. Unpublished one-file [FMalloc case-variant correction](../fixes/tow2-fmalloc-memory-leak.md),
   which resolves uppercase `Binned2` and stopped the observed unbounded-memory
   symptom in a sustained AFW run.

# 2026-07-20 beta.4 result

Reviewed PureDark's release and the five small `Joey-Merged` commits
through `7510c210`; no broad branch merge was performed. Ported only:

- corrected `mvScale[1]` calculation;
- AFW suspension/transition handling while UEVR 2D mode is active;
- beta.4 PDAFW ABI header changes;
- official beta.4 `PDAFWPlugin.dll` (white-speck/runtime fix).

The debug UINT64 shader option was deliberately not exposed or enabled.
Details: [AFW beta.4 motion-vector correction](../fixes/afw-beta4-motion-vector-scale.md).

TOW2 then reached gameplay and ran Previous Frame AFW with no visible weapon or
moving-object ghosting and smooth output. The log had one `InitFrameWarp`,
continuous `XR_SUCCESS`, no Present failures and no hang dump.

## Active experimental artifacts

- `UEVRBackend.dll` SHA-256:
  `8d67055a3154c5df1b4322986e6fbf07f976f58cd7223177198b78353d0bd675`
- `UEVRBackend.pdb` SHA-256:
  `bbfdc603c684be3ca40e11008e991985a30ed2ed0b49b1f35eef67f4c1b77983`
- PDAFW beta.4 SHA-256:
  `76bbc4d7a5370ba0d6a1a50de6b81d45223e42b764d2d7c79f0c6c48a59f6b64`
- OpenXR loader SHA-256:
  `3bb4eb190024f92eecf4a0bf33669b5d0b27571ddf4c36a33c58de199ee536c1`
- Exact zero-ghosting checkpoint:
  `<evidence-root>/puredark-beta4-tow2-zero-ghosting-success-20260720`
- Pre-beta.4 rollback:
  `<evidence-root>/pre-puredark-beta4-port-20260720`

The live TOW2 profile was reset after the successful run to Native startup with
Ghosting Fix/Bootstrap off; Previous Frame mode remains stored. Enable AFW and
ghosting only after reaching gameplay.

# SH2 unified Native-first checkpoint

The shipped branch line is `e0f7c5c7` → `75c172c5` → `a3d3128c`.
It pins UESDK `491f973a` and forces `SHProto-Win64-Shipping.exe` to Native
immediately before OpenXR swapchain creation. Repeated launches and a runtime
Native-to-AFW switch were reported working on backend
`eec89cd4e000ab83c0392caac59e73588125b71b8e054731f6472e7b7b08efbe`
with amended beta.4-derived PDAFW runtime
`b129118ba239e0c9fd7b0c803dab0199242af7142c7b9541e656e2f3eaca8ff9`.

This is not a general clearance of prior SH2 AFW failures. Current maintained
policy is SH2 Native-only with Native Stereo Fix on: later AFW attempts produced
`DXGI_ERROR_DEVICE_HUNG` and an NVIDIA `0x139` bugcheck. Do not test or publish
an SH2 AFW candidate unless explicitly requested. See
[Silent Hill 2](../games/silent-hill-2.md) and the
[Native cold-start fix](../fixes/sh2-shproto-afw-cold-start-native-force.md).

# SHf compatibility state

Published source commit `cc0c43f9` ports the historical Joey-lineage OpenXR
bootstrap into the beta.4 destination. Stable UI reuse, bounded setup retries,
owned scene/scene-capture copies, right-eye composition fallback, direct RHI
pose enqueue and rehook suppression produced reliable injection and both-eye
Native Stereo without runaway VRAM.

Alpha.2 packages that earlier Native-profile era. Its release archive remains
historical. The authoritative source tree now maintains a sanitized snapshot of
the rebuilt first-person/6DoF profile under `profiles/SHf-Win64-Shipping/`.
Install it cleanly rather than merging old scripts. The current profile starts
directly in Previous Frame AFW with Native Stereo Fix/Same Pass off, Ghosting
Fix/Bootstrap on and moving-object brightness correction off. Restart to change
modes; PDAFW has no teardown API.

The alpha.4 backend combines the TOW2 f37-equivalent FMalloc correction with an
SHf-only validated `FSceneViewFamily` layout/fail-closed constructor guard and
the Lua shutdown lifetime correction. It completed one approximately 35-minute
stable SHf run after PSVR2 runtime recovery; a later gameplay pass exposed only
the now-corrected exit-time Lua destructor call. Neither decisive SceneView
guard signature appeared. Repeated cold starts and broader Avowed/TOW2/SH2
regression remain required. Exact profile state, evidence and limitations are
in [Silent Hill f](../games/silent-hill-f.md).

# Standing findings

- Native → AFW has worked; **AFW → Native is unsafe** and can leave a black
  right eye because PDAFW exposes no teardown API. Restart to leave AFW.
- AFW performance must be judged by timing, not SteamVR FPS buckets. Prior
  Avowed testing saved about 8 ms while both modes stayed at 45 displayed FPS.
- fpsVR's application GPU number is incomplete for this multi-queue D3D12/AFW
  path; use elevated PresentMon/ETW or queue-aware GPU timestamps for aggregate
  cost.
- The beta.4 zero-ghosting cause is most likely the Y-scale correction plus the
  updated runtime. It has not been split into a risky two-variable A/B.

# Open validation

1. Repeat fresh SHf cold-Previous-Frame-AFW starts until the validated-layout or
   transient fail-closed SceneView signature is captured without the old
   `0x8/0x10` crash.
2. Exercise SHf loading, menus, weapon swaps, gameplay and clean exit across
   several fresh processes.
3. Regression-test the combined f37 plus SceneView candidate in Avowed, TOW2
   and SH2. Keep SH2 Native-only.
4. Optionally repeat TOW2 with live private-byte telemetry; do not invent a
   quantitative curve from the earlier timed-out monitor.
5. Continue TOW2 inventory/2D transitions, enrollment, swaps, death/load,
   native melee and controller-menu validation while preserving `_3dof`.
6. Repeat Avowed crafting, weapon replacement and loadout transitions against
   the untouched `_mine` behavior; keep hand/bone work parked.
7. Remove temporary diagnostics only after their corresponding race is proven
   resolved.
8. Keep alpha.4 marked prerelease until these checks pass; publication was
   explicitly approved, but stable promotion was not.

# Related

- [Avowed](../games/avowed.md)
- [The Outer Worlds 2](../games/outer-worlds-2.md)
- [Silent Hill f](../games/silent-hill-f.md)
- [Silent Hill 2](../games/silent-hill-2.md)
- [Checkpointing and recovery](../playbooks/checkpoint-and-recovery.md)

# Citations

- `<private-worklog-root>/PUREDARK_AFW_AVOWED_WORKLOG.md`
- `<private-worklog-root>/PUREDARK_AFW_TOW2_WORKLOG.md`
- [PureDark beta.4 release](https://github.com/PureDark/UEVR/releases/tag/UEVR_AFW_v1.0-beta.4)

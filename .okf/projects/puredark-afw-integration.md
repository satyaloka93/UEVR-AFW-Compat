---
type: project
title: PureDark AFW integration — ongoing effort state
description: Live state of the narrow Avowed/TOW2/SHf compatibility port onto PureDark AFW; TOW2 beta.4 Previous Frame is validated, and source commit cc0c43f9 plus alpha.2 publish SHf's working experimental Native/AFW candidate and profiles.
tags:
- afw
- puredark
- avowed
- tow2
- shf
- ongoing
timestamp: '2026-07-22T10:48:00+09:00'
resource: .
---

# Goal and scope

Run PureDark AFW in games already stabilized on the regular backend without
importing broad lifecycle, renderer, VRS/UI, or artifact-prone behavior. The
standing rules are in [narrow port scope](../decisions/narrow-port-scope.md).

# Branch and preservation state

- Public branch: `afw-beta4-game-compat`, based on PureDark
  `AFW` commit `e260ffe8` (`UEVR_AFW_v1.0-beta.4` tag).
- The public source directly pins tested hardened UESDK `9034a857` in gated
  private `PureDark/UESDK`; authorized builders fetch it through the ordinary
  recursive submodule workflow.
- Joey integration is preserved at `021d45b7` plus private stashes/evidence;
  only audited corrections are intended for publication with attribution.
- Experimental deployment remains isolated at `<deploy-dir>`; the primary UEVR
  installation remains untouched.
- The Avowed/TOW2 integration and SHf candidate source are published
  experimentally on this branch. Open validation remains explicit; publication
  does not promote them to stable status.

# Runtime and build dependency model

- UESDK remains a gated `PureDark/UESDK` submodule. Builders must link GitHub to
  Epic, obtain repository access, configure SSH, and initialize submodules; its
  source is not copied into the public fork.
- The checked-in `PDAFWPlugin.h` defines the ABI and the dummy implementation
  supplies no-op link-time exports. Real frame warping requires the official
  matching PureDark `PDAFWPlugin.dll`, obtained separately.
- The beta.4 runtime/header/backend callers form one compatibility checkpoint.
  The validated runtime SHA-256 is
  `76bbc4d7a5370ba0d6a1a50de6b81d45223e42b764d2d7c79f0c6c48a59f6b64`.
- See repository file `docs/PDAFW_RUNTIME.md` before building or packaging;
  never mistake the generated dummy DLL for the real runtime.

# Public alpha releases

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
- Hardened UESDK + Avowed-only AddObject candidate guard.
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

# SHf local compatibility candidate

Published source commit `cc0c43f9` ports the Joey-derived UE5.7/OpenXR
bootstrap into the beta.4 destination. Stable UI reuse, bounded setup retries,
owned scene/scene-capture copies, right-eye composition fallback, direct RHI
pose enqueue and rehook suppression produced reliable injection and both-eye
Native Stereo without runaway VRAM.

Manual Native → Previous Frame AFW is functional, but it saves only about 3–4
ms after DLSS is correctly reapplied, remains CPU-limited near 40–41 application
FPS, and distorts hands/weapons. Native plus a manual game-owned DLSS quality
reapply is therefore recommended. AFW startup, AFW → Native and automatic DLSS
settings reflection are rejected. The exact profile, measurements, hashes and
limitations are in [Silent Hill f](../games/silent-hill-f.md).

Alpha.2 includes the candidate backend and clean profile. Alpha.1 remains an
immutable historical release whose SHf unsupported warning is still correct.

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

1. Repeat several clean TOW2 Native-title launches; capture a fresh in-process
   dump if the analyzer race returns.
2. Test inventory and any profile-driven 2D-screen transition repeatedly.
3. Resolve right-controller UEVR-menu selection without dragging the framework
   window or rotating the game menu/camera.
4. Confirm saves, shadows, both eyes, weapon scripts and sustained gameplay.
5. Repeat Avowed crafting, weapon replacement and loadout transitions; confirm
   the stale-attachment guard protects the original dump path without breaking
   weapon recovery.
6. Reproduce the alpha.2 SHf backend/profile hashes across sustained Native
   gameplay and transitions; keep AFW experimental until image quality and
   lifecycle limits are resolved.
   Separately regression-test actual SH2 and PSVR2 input with beta.4.
7. Remove the temporary hang-dump watchdog after launch reliability is proven.
8. Keep releases marked prerelease until these checks pass; retain temporary
   diagnostics only while their corresponding race remains unresolved.

# Related

- [Avowed](../games/avowed.md)
- [The Outer Worlds 2](../games/outer-worlds-2.md)
- [Silent Hill f](../games/silent-hill-f.md)
- [Checkpointing and recovery](../playbooks/checkpoint-and-recovery.md)

# Citations

- `<private-worklog-root>/PUREDARK_AFW_AVOWED_WORKLOG.md`
- `<private-worklog-root>/PUREDARK_AFW_TOW2_WORKLOG.md`
- [PureDark beta.4 release](https://github.com/PureDark/UEVR/releases/tag/UEVR_AFW_v1.0-beta.4)

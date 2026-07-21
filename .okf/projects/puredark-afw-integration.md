---
type: project
title: PureDark AFW integration — ongoing effort state
description: Live state of the narrow Avowed/TOW2 compatibility port onto PureDark AFW; TOW2 beta.4 Previous Frame is now smooth with zero observed ghosting, while intermittent title timing and cross-game regressions remain open.
tags:
- afw
- puredark
- avowed
- tow2
- ongoing
timestamp: '2026-07-20T19:41:50+09:00'
resource: .
---

# Goal and scope

Run PureDark AFW in games already stabilized on the regular backend without
importing broad lifecycle, renderer, VRS/UI, or artifact-prone behavior. The
standing rules are in [narrow port scope](/decisions/narrow-port-scope.md).

# Branch and preservation state

- Public documentation branch: `afw-beta4-game-compat`, based on PureDark
  `AFW` commit `e260ffe8` (`UEVR_AFW_v1.0-beta.4` tag).
- The validated local integration uses hardened UESDK descendant `9034a857`
  instead of PureDark's original `491f973`; the public branch remains at the
  beta.4 source baseline until the compatibility source passes its open gates.
- Joey integration is preserved at `021d45b7` plus private stashes/evidence;
  only audited corrections are intended for publication with attribution.
- Experimental deployment remains isolated at `<deploy-dir>`; the primary UEVR
  installation remains untouched.
- The compatibility working tree is intentionally uncommitted and is **not yet
  present on the public branch**. Do not infer that this documentation-preview
  branch contains the described Avowed/TOW2 source changes.

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
[/fixes/avowed-stale-attachment-guard.md](/fixes/avowed-stale-attachment-guard.md).
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
2. [TOW2-only analyzer threshold 40](/fixes/tow2-view-extension-analyzer-threshold.md).
3. [Per-call FUObjectArray-backed AddObject validation](/fixes/tow2-addobject-candidate-guard.md), derived from a running-process dump.
4. One-shot [in-process title hang dump](/playbooks/in-process-hang-dump.md)
   temporarily retained because analyzer completion is still timing-sensitive.

# 2026-07-20 beta.4 result

Reviewed PureDark's release and the five small `Joey-Merged` commits
through `7510c210`; no broad branch merge was performed. Ported only:

- corrected `mvScale[1]` calculation;
- AFW suspension/transition handling while UEVR 2D mode is active;
- beta.4 PDAFW ABI header changes;
- official beta.4 `PDAFWPlugin.dll` (white-speck/runtime fix).

The debug UINT64 shader option was deliberately not exposed or enabled.
Details: [AFW beta.4 motion-vector correction](/fixes/afw-beta4-motion-vector-scale.md).

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
6. SHf is currently non-working: reconcile and port its Joey-derived UE5.7/
   OpenXR bootstrap into PureDark AFW before regression testing it. Separately
   regression-test SH2 and PSVR2 input with beta.4.
7. Remove the temporary hang-dump watchdog after launch reliability is proven.
8. Do not commit the integration source until those checks pass.

# Related

- [Avowed](/games/avowed.md)
- [The Outer Worlds 2](/games/outer-worlds-2.md)
- [Checkpointing and recovery](/playbooks/checkpoint-and-recovery.md)

# Citations

- `<private-worklog-root>/PUREDARK_AFW_AVOWED_WORKLOG.md`
- `<private-worklog-root>/PUREDARK_AFW_TOW2_WORKLOG.md`
- [PureDark beta.4 release](https://github.com/PureDark/UEVR/releases/tag/UEVR_AFW_v1.0-beta.4)

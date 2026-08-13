---
type: fix
title: SHf FSceneViewFamily validated layout and fail-closed constructor guard
description: Prevent intermittent SHf startup AVs by publishing the source-confirmed scene-family layout only after live structural/vtable validation and skipping scene-dependent remapping when the constructor receives an unproven family.
tags:
- silent-hill-f
- shf
- fsceneviewfamily
- startup-race
- validation
- fail-closed
- afw
timestamp: '2026-08-08T19:35:00+09:00'
resource: src/mods/vr/FFakeStereoRenderingHook.cpp
---

# Problem

SHf intermittently crashed in `FFakeStereoRenderingHook::sceneview_constructor()`
while reading `FSceneViewFamily::SceneInterface`. Failed discovery accepted a
blind fallback (`render target=0x8`, `scene interface=0x10`) and the constructor
treated any non-null family pointer as safe. The fatal access was a read from
`0xffffffffffffffff`.

The game's fingerprint-matched retained discovery cache and UE source layout
agree on a different layout:

- views: `0x8`
- render target: `0x30`
- scene interface: `0x38`

Identical f37/profile starts both failed and succeeded, and the same crash
existed before f37. This is a timing-sensitive late-discovery race, not allocator
behavior.

# Narrow implementation

The local candidate retains the TOW2 f37 FMalloc correction and changes only the
SHf scene-family call site plus a small offset-publication API:

1. On SHf only, require the live family range to cover the source layout.
2. Require module-backed vtables and module-backed first virtual functions for
   the family, render target and scene interface.
3. Validate the views `TArray` shape (`0 <= count <= capacity <= 16`) and its
   backing range when non-empty.
4. Only after every check succeeds, publish `0x8/0x30/0x38` as the validated
   offsets.
5. If validation is not yet possible, set the local family to null for
   scene-dependent remapping and continue through the original constructor.
   A later constructor naturally retries; no scan loop is added.
6. Revalidate the resulting scene-interface object before using its identity.

This deliberately does **not** port the rejected early Joey
`FSceneViewFamily` cache revision that caused repeated Avowed scans. Non-SHf
games retain the existing discovery path.

The design follows [render-target validation hardening](render-target-validation-hardening.md): positively validate or skip; never dereference a guessed layout.

# Candidate, publication and rollback

Runtime-tested pre-publication provenance:
`70f6e508+local-f37-shf-sceneview-guard`.

- backend SHA-256: `ecf7ac708e39e4c7a513d9749a35155a181bd095b1a31e16b2be543361198e49`
- PDB SHA-256: `f4e49192cc3c3d38f2544a069d31e7b584c1aeb04fd261530a91a416d06b4e49`
- PDAFW remained unchanged.

UESDK commit `7610b01` publishes the small validated-offset API and parent
backend commit `b64dacc8` publishes the SHf call-site guard in alpha.4.
Immediate historical rollback for the local test remains the f37-only candidate
(`ca825f27...`). The alpha.4 release build has separately generated hashes.

# Required validation

Use fresh SHf processes with the retained cold-start Previous Frame AFW profile,
Native Stereo Fix/Same Pass off, Ghosting/Bootstrap on and moving-object
brightness correction off.

Expected decisive signature:

`[SHf] Published validated FSceneViewFamily layout views=0x8 render_target=0x30 scene=0x38`

Transient early families may instead log the fail-closed message; they must not
produce the old `0x8/0x10` fallback crash. Repeat startup several times, then
exercise loading, menus, weapon swaps and clean exit. Avowed, TOW2 and SH2 must
be regression-tested before promotion.

# First runtime attempt was invalidated by PSVR2/SteamVR failure

The first candidate run did not reproduce the SceneView access violation. UE
recorded an assert/abort on the RHI thread in
`vrclient_x64 -> xrPollEvent -> OpenXR::consume_events`, not a
`sceneview_constructor` frame.

SteamVR logs identify the upstream sequence:

1. the PSVR2 driver reported its frame stuck at point 556 for more than ten
   seconds;
2. `vrserver` watchdog-aborted the `playstation_vr2` `server_main` thread;
3. the OpenXR client lost VR Server and entered `EXITING`;
4. five seconds later `vrclient_x64` asserted because the quit event had not
   completed;
5. after SteamVR restarted with SHf gone, PSVR2 independently hit the same
   watchdog timeout again.

The independent second failure excludes the candidate/game as the initiating
cause. This run provides no SceneView validation result; keep the candidate and
fully recover SteamVR/PSVR2 before repeating from a fresh process.

# First stable run

After runtime recovery, the same candidate/profile completed an approximately
35-minute SHf run without a crash. No new UE crash report, FMalloc failure,
GPU/device-hung signature or PSVR2 watchdog appeared, and SteamVR later exited
gracefully.

Neither the validated-layout publication message nor the transient fail-closed
message appeared, however. The guarded constructor path was not observed, so
this is a broad stability pass rather than decisive evidence that the new guard
intercepted the formerly fatal transient family. Repeat fresh starts until a
guard signature is captured.

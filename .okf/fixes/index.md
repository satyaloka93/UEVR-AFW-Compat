# Fixes

Individual engine/runtime corrections, each written to be reusable when a new
game shows the same symptom.

## Stereo hook & discovery

* [Vtable scan widening](stereo-vtable-scan-widening.md) — game patch moved the FFakeStereoRendering reference; scan 100 → 300 bytes.
* [Render-target validation hardening](render-target-validation-hardening.md) — UESDK: validate offsets, never blind-fallback.
* [SDK discovery cache port](sdk-discovery-cache-port.md) — per-launch offset discovery is a startup race against UEVR's own hooks; cache validated discoveries (2 minimal UESDK commits), reject the per-frame-rescan third commit.

## Native stereo stability

* [Safe activation & self-disable](native-stereo-safe-activation.md) — safe point, passthrough, degrade-not-crash, VEH scoping.
* [Hogwarts transition cooldown](hogwarts-transition-cooldown.md) — extra cooldown after load/world transitions.
* [Virtual Shadow Maps mismatch](virtual-shadow-maps-stereo-mismatch.md) — `r.Shadow.Virtual.Enable 0` fixes per-eye shadow mismatch.

## Present / OpenXR pipeline

* [SHf UE5.7 OpenXR bootstrap](shf-ue57-openxr-bootstrap.md) — validated startup discovery, recoverable OpenXR ownership and bounded D3D12 scene/UI resources, published in SHf source commit `cc0c43f9`.
* [SHf profile rebuild (main.lua removal)](shf-profile-rebuild-main-lua-removal.md) — delete the monolithic main.lua, re-implement its gameplay wiring in 3 standalone scripts, add a settle gate/reload hardening, and maintain the sanitized working first-person/6DoF package.
* [SHf FSceneViewFamily fail-closed guard](shf-sceneviewfamily-fail-closed.md) — validate the source-confirmed `0x8/0x30/0x38` layout before publication and skip transient scene remapping instead of dereferencing guessed `0x8/0x10` fallbacks.
* [Authoritative wait-frame time](openxr-authoritative-wait-frame.md) — fixes XR_ERROR_TIME_INVALID / frame-discarded loops.
* [Backbuffer fallback + early XR frame prep](tow2-backbuffer-fallback-openxr-prep.md) — title/startup Present-path survival (TOW2).
* [CVar scanner bypass](tow2-cvar-scanner-bypass.md) — decisive fix for the original TOW2 title-splash stall.
* [TOW2 analyzer threshold](tow2-view-extension-analyzer-threshold.md) — TOW2-only 40-sample discovery correction; still timing-sensitive.
* [TOW2 AddObject candidate guard](tow2-addobject-candidate-guard.md) — per-call FUObjectArray/class-hierarchy validation derived from a live-stall dump.
* [TOW2 explicit dynamic-component enrollment](tow2-explicit-component-enrollment.md) — after guarded discovery misses a late Steam weapon, enroll only an explicitly requested scene component after exact array/vtable/hierarchy validation; restores the missing 6DoF attachment gate without broad scanning.
* [TOW2 FMalloc discovery and memory growth](tow2-fmalloc-memory-leak.md) — a 30 GB-and-rising run logged `Failed to find GMalloc`; TOW2 exposes uppercase `Binned2`, matching Praydog's narrow case-sensitive allocator discovery/memory-leak fix that the maintained UESDK branch does not yet contain.
* [SH2 AFW cold-start / Native-force](sh2-shproto-afw-cold-start-native-force.md) — shipped unified branch pins baseline UESDK and forces Native before swapchain init; runtime AFW remains exact-checkpoint validation rather than a general safety claim.

## AFW image quality and lifecycle

* [PureDark beta.4 motion-vector scale](afw-beta4-motion-vector-scale.md) — correct Y scale + updated runtime + 2D transition guards; TOW2 zero-ghosting result.

## Lua lifecycle

* [Lua ScriptContext shutdown logging guard](lua-scriptcontext-shutdown-log.md) — remove an exit-time diagnostic call through a plugin API function table that may already have been destroyed.

## Avatar & input

* [Avowed local-avatar resolver + parked native-bone experiment](local-avatar-native-bone-driver.md) — reusable avatar/proxy/lifetime diagnostics separated from an uncommitted, unproven hand-bone writer that remains off.
* [Avowed stale attachment guard](avowed-stale-attachment-guard.md) — reject invalidated crafting/loadout components before attachment ProcessEvent.
* [OpenVR analog trigger fallback](openvr-analog-trigger-fallback.md) — TriggerValue action + binding preservation.
* [PSVR2 Triangle d-pad](psvr2-triangle-dpad.md) — PSVR2-on-SteamVR input aliasing and R3 roles.

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
* [SHf profile rebuild (main.lua removal)](shf-profile-rebuild-main-lua-removal.md) — delete the monolithic main.lua, re-implement its gameplay wiring in 3 standalone scripts, add a settle gate and reload hardening.
* [Authoritative wait-frame time](openxr-authoritative-wait-frame.md) — fixes XR_ERROR_TIME_INVALID / frame-discarded loops.
* [Backbuffer fallback + early XR frame prep](tow2-backbuffer-fallback-openxr-prep.md) — title/startup Present-path survival (TOW2).
* [CVar scanner bypass](tow2-cvar-scanner-bypass.md) — decisive fix for the original TOW2 title-splash stall.
* [TOW2 analyzer threshold](tow2-view-extension-analyzer-threshold.md) — TOW2-only 40-sample discovery correction; still timing-sensitive.
* [TOW2 AddObject candidate guard](tow2-addobject-candidate-guard.md) — per-call FUObjectArray/class-hierarchy validation derived from a live-stall dump.
* [TOW2 explicit dynamic-component enrollment](tow2-explicit-component-enrollment.md) — after guarded discovery misses a late Steam weapon, enroll only an explicitly requested scene component after exact array/vtable/hierarchy validation; restores the missing 6DoF attachment gate without broad scanning.
* [SH2 AFW cold-start / Native-force](sh2-shproto-afw-cold-start-native-force.md) — shipped unified branch pins baseline UESDK and forces Native before swapchain init; runtime AFW remains exact-checkpoint validation rather than a general safety claim.

## AFW image quality and lifecycle

* [PureDark beta.4 motion-vector scale](afw-beta4-motion-vector-scale.md) — correct Y scale + updated runtime + 2D transition guards; TOW2 zero-ghosting result.

## Avatar & input

* [Local avatar + native bone driver](local-avatar-native-bone-driver.md) — resolve the real player avatar; drive hand bones.
* [Avowed stale attachment guard](avowed-stale-attachment-guard.md) — reject invalidated crafting/loadout components before attachment ProcessEvent.
* [OpenVR analog trigger fallback](openvr-analog-trigger-fallback.md) — TriggerValue action + binding preservation.
* [PSVR2 Triangle d-pad](psvr2-triangle-dpad.md) — PSVR2-on-SteamVR input aliasing and R3 roles.

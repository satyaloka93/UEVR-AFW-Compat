# Fixes

Individual engine/runtime corrections, each written to be reusable when a new
game shows the same symptom.

## Stereo hook & discovery

* [Cross-game script controls and SHf CVars](cvar-script-controls-shf.md) — saved auto-apply/bypass, explicit one-shot scripts and no-op/conflict handling; first SHf VSM readback and visual improvement confirmed.

* [UObject browser array validation](uobject-browser-array-validation.md) — dump-proven name-formatting crash; live object guards, interface stride correction and bounded object-array pages; runtime retest pending.

* [TOW2 validated CVar access](tow2-validated-cvar-access.md) — extended setter/getter ABI correction, bounded registry discovery, verified VSM application and CPU pipeline timing; live menu/image correction confirmed, performance attribution pending.

* [TOW2 AFW GPU copy and resource-state guard](tow2-afw-gpu-copy-guard.md) — fail-closed D3D12 copy/barrier handling after a DRED `CopyResource` GPU crash, plus the authoritative publish-tree deployment correction.

* [RenoDX API18 status text](renodx-api18-status-text.md) — restore hidden addon Waiting/Active status; NR execution remains unproven.

* [Vtable scan widening](stereo-vtable-scan-widening.md) — game patch moved the FFakeStereoRendering reference; scan 100 → 300 bytes.
* [Render-target validation hardening](render-target-validation-hardening.md) — UESDK: validate offsets, never blind-fallback.
* [SDK discovery cache port](sdk-discovery-cache-port.md) — per-launch offset discovery is a startup race against UEVR's own hooks; cache validated discoveries (2 minimal UESDK commits), reject the per-frame-rescan third commit.

## Native stereo stability

* [Safe activation & self-disable](native-stereo-safe-activation.md) — safe point, passthrough, degrade-not-crash, VEH scoping.
* [Hogwarts transition cooldown](hogwarts-transition-cooldown.md) — extra cooldown after load/world transitions.
* [Virtual Shadow Maps mismatch](virtual-shadow-maps-stereo-mismatch.md) — `r.Shadow.Virtual.Enable 0` fixes per-eye shadow mismatch.

## Present / OpenXR pipeline

* [Native GPU cost attribution](native-openxr-cost-attribution.md) — sampled final-copy GPU timing, CPU wait breakdown and correlated Cheeky costs; no added GPU waits.

* [SHf UE5.7 OpenXR bootstrap](shf-ue57-openxr-bootstrap.md) — validated startup discovery, recoverable OpenXR ownership and bounded D3D12 scene/UI resources, published in SHf source commit `cc0c43f9`.
* [SHf profile rebuild (main.lua removal)](shf-profile-rebuild-main-lua-removal.md) — delete the monolithic main.lua, re-implement its gameplay wiring in 3 standalone scripts, add a settle gate/reload hardening, and maintain the sanitized working first-person/6DoF package.
* [SHf FSceneViewFamily fail-closed guard](shf-sceneviewfamily-fail-closed.md) — validate the source-confirmed `0x8/0x30/0x38` layout before publication and skip transient scene remapping instead of dereferencing guessed `0x8/0x10` fallbacks.
* [Authoritative wait-frame time](openxr-authoritative-wait-frame.md) — Avowed timing override plus Native Stereo stale-time guard and bounded delivery counters; Native runtime validation pending.
* [Backbuffer fallback + early XR frame prep](tow2-backbuffer-fallback-openxr-prep.md) — title/startup Present-path survival (TOW2).
* [CVar scanner bypass](tow2-cvar-scanner-bypass.md) — decisive fix for the original TOW2 title-splash stall.
* [TOW2 analyzer threshold](tow2-view-extension-analyzer-threshold.md) — TOW2-only 40-sample discovery correction; still timing-sensitive.
* [TOW2 AddObject candidate guard](tow2-addobject-candidate-guard.md) — per-call FUObjectArray/class-hierarchy validation derived from a live-stall dump.
* [TOW2 explicit dynamic-component enrollment](tow2-explicit-component-enrollment.md) — after guarded discovery misses a late Steam weapon, enroll only an explicitly requested scene component after exact array/vtable/hierarchy validation; restores the missing 6DoF attachment gate without broad scanning.
* [TOW2 FMalloc discovery and memory growth](tow2-fmalloc-memory-leak.md) — a 30 GB-and-rising run logged `Failed to find GMalloc`; TOW2 exposes uppercase `Binned2`, matching Praydog's narrow case-sensitive allocator discovery/memory-leak fix that the maintained UESDK branch does not yet contain.
* [SH2 AFW cold-start / Native-force](sh2-shproto-afw-cold-start-native-force.md) — shipped unified branch pins baseline UESDK and forces Native before swapchain init; runtime AFW remains exact-checkpoint validation rather than a general safety claim.

## AFW image quality and lifecycle

* [PureDark beta.4 motion-vector scale](afw-beta4-motion-vector-scale.md) — correct Y scale + updated runtime + 2D transition guards; TOW2 zero-ghosting result.

## DLSS 5 Neural Rendering

* [DLSS 5 Neural Rendering addon host](dlss5-neural-rendering-addon-host.md) — UEVR exports the ReShade addon API itself; both eyes get neural rendering because their cache keys are identical.
* [ReShade host object model and versioned ImGui tables](reshade-host-object-model.md) — what a newer RenoDX addon needs from the host, and the workset-pool blocker that keeps 4.1.5 installed.
* [Foveated DLSS 5 Neural Rendering](dlss5-foveated-neural-rendering.md) — a tail-JMP thunk beats the signed snippet's return-address check, so NR's subrects can be rewritten per frame; carries the real NGX parameter vtable order (slot 1 is the resource setter, not float) and the stale-periphery fix.

## Lua lifecycle

* [Lua ScriptContext shutdown logging guard](lua-scriptcontext-shutdown-log.md) — remove an exit-time diagnostic call through a plugin API function table that may already have been destroyed.

## Avatar & input

* [Avowed local-avatar resolver + parked native-bone experiment](local-avatar-native-bone-driver.md) — reusable avatar/proxy/lifetime diagnostics separated from an uncommitted, unproven hand-bone writer that remains off.
* [Avowed stale attachment guard](avowed-stale-attachment-guard.md) — reject invalidated crafting/loadout components before attachment ProcessEvent.
* [OpenVR analog trigger fallback](openvr-analog-trigger-fallback.md) — TriggerValue action + binding preservation.
* [PSVR2 Triangle d-pad](psvr2-triangle-dpad.md) — PSVR2-on-SteamVR input aliasing and R3 roles.

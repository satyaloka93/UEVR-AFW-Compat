---
type: fix
title: RenoDX API18 status text in the UEVR addon host
description: Forward ImGui 19250 colored status text so missing NR execution is not hidden by the partial UI host.
tags: [renodx, dlss, imgui, diagnostics]
timestamp: '2026-09-09'
---

The new `renodx-dlss.addon64` (SHA256 `FBA3271626587F8B1F49C4FB40BBE23FD969282DA6FE48BE2DA9B17BEB708EE6`, addon API18, ImGui19250) is distinct from the older 4.x `renodx-dlss5` described in [the earlier host investigation](reshade-host-object-model.md). Do not transfer that addon's settings schema or runtime conclusions to this one.

Its Waiting/Active status renderer calls binary RVA `0x40900`, which dispatches ImGui table offset `0x348`: slot105, `TextColoredV`. Leaving this slot stubbed hides the addon's own status even when controls draw. The host now forwards slots105–109 (colored, disabled, wrapped, label and bullet text) using the published signatures. Colored text is logged under `DLSSNR-ADDON-STATUS`, bounded to 32 unique lines per process and 511 bytes per diagnostic line. Rendering retains the original argument list via a separate `va_copy` for logging.

Standalone tests cover formatted status text, diagnostic truncation, layout advance and color restoration; Windows Release compilation passed. Runtime display still needs validation. This is a diagnostic/UI fix, not proof of NR output. An attached NVIDIA evaluation hook with zero calls remains inactive NR evidence. Source/resource validation must not be bypassed to make its indicator green.

## NGX execution investigation

The earlier fe120/fe3ed diagnostic probes do not cover the NGX post-evaluation callback. Zero hits there cannot establish that all NR paths were unentered. For the exact addon hash above, reconstruction wrapper0x66fd0 dispatches post callbacks including0x869f0. After result/mode/input checks, this callback requires native command-list GetPrivateData GUID `966aba18-ea54-4558-a09c-21ee51ca244b` to return an eight-byte ReShade command-list pointer. The addon supplies/removes this tag through command-list lifecycle callbacks (setter0x96da0, registered event2). UEVR does not currently provide per-native-list lifecycle/identity; its separate fake-object private-data table is not native D3D12 private data.

The September9 15:50–15:52 run confirms this rejection at runtime: an observed summary has11541 native-wrapper queries and11541 rejections; sampled results are0x887a0002, size0, no pointer, with Output/MotionVectors/Depth and892x909 render dimensions present. Raw mode changes with menu selections. Do not bypass the tag check or publish the shared temporary fake command-list pointer.

`AddonCommandListLifetime.hpp` is an isolated, WARP-tested ownership component, not a ReShade ABI or an enabled runtime integration. It retains/canonicalizes per-list COM identity, separates private data, bounds retention and requires active leases to finish before paired teardown. Its simulated callback tests do not run RenoDX. Foreign-device rejection is implemented but its test skips when WARP returns a singleton device.

Activation remains unsafe without downstream GPU methods: source evaluation uses command-list barriers, resource copies and state restoration, while current host GPU methods remain stubs. A native tag alone would allow addon bookkeeping to advance without the required GPU commands. Complete the adapter and synchronization before deploying an execution fix. Exact deployments and test instructions are maintained in `docs/TOW2_NEURAL_RENDERING_TEST_PLAN.md`.

`AddonCommandListGpu.hpp` now implements isolated native transition/UAV-barrier and strict whole-resource copy helpers, with restricted API18 usage conversion matching ReShade's constant-buffer/common/present/combined-depth rules. Actual WARP buffer and texture readbacks passed, with no D3D12 debug-layer errors. This does not change the runtime host's stubs. Resource provenance, full ABI forwarding, binding/state restoration and lifecycle/reset integration remain required. Unsupported usages and invalid batches return failure; an eventual void-returning addon adapter must not ignore that failure and continue NR execution.

# Citations

`AddonShaderBindings.hpp` adds a separate, still-unwired shader-binding recorder/snapshot: PSO, compute/graphics root signatures, partial constants, root CBV/SRV/UAV addresses, shader-visible heaps and finite descriptor tables. Changes invalidate stale arguments; snapshots are restricted to one native list/recording. Root layouts require the actual creation blob. The WARP compute test overwrites all supported compute bindings then restores and dispatches without manual rebinding; both root-UAV and table-UAV readbacks match baseline, with no D3D12 debug-layer errors. Graphics draw behavior is not tested. This is not full pipeline state restoration: native hook coverage, resource lifetimes, graphics fixed-function/IA state and unknown execution effects still require handling before runtime activation.

Hover help additionally requires slots218–222: BeginTooltip, EndTooltip, SetTooltipV, BeginItemTooltip and SetItemTooltipV. `AddonTooltip19250.hpp` now forwards these with word wrapping capped to the display width and approximately 35 font-width units. Only addon-owned tooltip windows are closed; the guarded overlay closes an unfinished addon tooltip. Standalone tests cover multiline layout, nested-tooltip rejection and returning to the host window. In-game hover behavior remains to be checked.

The user's GTA flat-mode screenshots are transcribed in `docs/RENODX_DLSS_TOOLTIPS.md`. They show four hook choices (Off/Auto/Upscaled/Present), correcting the earlier three-choice inference. A saved numeric mode must not be labeled Upscaled without verifying its mapping. The displayed Global Tone tooltip explicitly disclaims visible effect in the recovered NGX path; this does not explain every inactive NR control.

- [ReShade ImGui19250 table](https://github.com/crosire/reshade/blob/main/source/imgui_function_table_19250.hpp)
- [ReShade D3D12 resource-state conversion](https://github.com/crosire/reshade/blob/main/source/d3d12/d3d12_impl_type_convert.cpp)
- [Microsoft root-signature binding/invalidation semantics](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-a-root-signature)
- [Microsoft descriptor-heap binding semantics](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-setdescriptorheaps)
- Local source: `src/mods/AddonText19250.hpp`, `src/mods/DlssNeuralRendering.cpp`, `scripts/test-addon-style.cpp`.
- Deployment and run evidence: `docs/TOW2_NEURAL_RENDERING_TEST_PLAN.md`.

# TOW2 UEVR neural-rendering investigation — 2026-09-08

## Corrections and priorities

- User confirms Silent Hill f's left/right brightness mismatch was caused by Native Stereo Fix. Keep that setting OFF for SHf. Do not generalize this requirement to other games or attribute the mismatch to Cheeky/OFXR based on the earlier overlapping changes.
- Cheeky eye tracking is still unresolved. Prioritize an isolated TOW2 UEVR neural-rendering test now, then OFXR improvement as a separate track. Rounded foveation is deferred until the existing rectangle is validated.
- The previous SHf "off/off" label was premature: the assistant had not checked live modules. The 08:56–08:57 Cheeky log reports layer=0; the registry inspection found no OFXR registration and user CHEEKY_OPENXR_LAYER_DISABLE=1. These support, but do not replace, a live module inventory. Game was closed when inspected.
- Off/off means Cheeky OpenXR layer and OFXR layer only; it does not imply Cheeky's addon, ReShade, or every other OpenXR layer is absent. setx affects future inherited environments; it does not unload a running process.

## TOW2 findings

### Shader-binding snapshot/restoration — September 9, after GPU helper stage

Added isolated `AddonShaderBindings.hpp`. Root metadata is parsed from the exact serialized blob associated with a native root signature. The recorder keeps separate compute/graphics banks, partial root-constant masks, CBV/SRV/UAV addresses, descriptor tables, shader-visible heaps and the current PSO. Changed signatures invalidate their arguments; redundant same-signature sets preserve them. Heap changes invalidate tables. Table bases and declared finite spans must fit the bound heap. Unbounded tables, direct heap indexing and local signatures are rejected in this implementation.

Snapshots retain list/PSO/root-signature/heap objects and are bound to one native list and a shared recording-validity token. Reset invalidates every snapshot from the previous recording; explicit invalidation handles unknown execution effects. Restoring onto another list or from incomplete/invalid state returns false before recording commands. Resources referenced through GPU addresses/descriptors still need external fence-aware lifetime handling. The recorder must receive every relevant native setter from a known recording boundary; late injection without that history cannot be treated as complete capture.

The expanded Windows WARP test compiles two compute shaders, binds a baseline PSO/root signature/root constants/CBV/SRV/UAV/descriptor heap/table, dispatches once, overwrites every supported compute binding, restores, and dispatches again without manual rebinding. Root-UAV readback matches20/20 and descriptor-table UAV readback matches30/30. D3D12 debug layer reports no errors. Partial constants, signature/heap invalidation, wrong-list rejection and snapshot invalidation tests pass; prior copy/texture/lifetime tests still pass. UAV ordering barriers are now exercised by the two dispatches. Graphics-bank replay exists but is not validated by a graphics draw test.

This is a shader-binding subset, NOT complete D3D12 state capture. Render targets, viewports/scissors, IA state, raytracing PSOs, bundles/ExecuteIndirect effects, and resource states are not restored here. Runtime native-setter/root-creation/reset hooks and the ReShade adapter are not wired to the component. Next integration must establish resource provenance/lifetimes, observe native bindings from valid recording boundaries, implement required ABI methods, and dispatch command-list lifecycle with synchronized teardown. Keep NR execution gated until the actually exercised downstream path is covered. No backend build/deployment, addon replacement or user configuration changes; no game retest requested.

### Native GPU barrier/copy helpers — September 9, after lifetime stage

Added `AddonCommandListGpu.hpp`, still isolated from the runtime host. It translates a restricted set of API18 usages to D3D12 states, including separate constant-buffer usage, common/present and ReShade's combined-depth semantics. Undefined, unknown and incompatible write combinations are rejected. Whole-resource transitions and per-resource UAV barriers are recorded on the native list. Barrier batches are completely validated before command emission. CopyResource uses native destination/source order while exposing source/destination order and rejects null/self copies or mismatched descriptions. Typeless-family copies, aliasing/global barriers and enhanced barriers are not supported. Callers must supply live same-device resources, an open direct list and accurate states; this helper does not validate arbitrary handles or infer resource state.

Expanded `test-addon-command-list.exe` with `test-addon-command-list-gpu.cpp`. Actual WARP GPU buffer copy/transition/readback matches1024 bytes. A separate8x8 RGBA texture upload, whole-texture copy, transition and footprint-aware readback matches every pixel. Invalid-batch, mismatched-size, self-copy and usage conversion tests passed. D3D12 debug layer was available and reported no errors. Existing lifetime tests also passed; the previously documented foreign-device test remains skipped for WARP's singleton device. UAV emission is implemented but not exercised by this test; no comprehensive command-list ABI or RenoDX execution claim.

Remaining before runtime activation: resource handle provenance/lifetimes, complete required ReShade ABI forwarding, pipeline/descriptor/root-binding state tracking and restoration around NR, command-list lifecycle dispatch before NGX evaluation, and synchronized device/reset teardown. The new helpers do not enable these paths or publish RenoDX's native tag. No backend/game-addon deployment, settings or registry changes. Keep the existing diagnostic build; no game retest requested for this intermediate component.

### Native command-list lifetime component — September 9, after 15:52 run

Runtime now confirms the missing tag: at15:51:29 the NGX gate summary reports entry13755, mode13755, inputs11541, native-wrapper11541, wrapper-rejected11541. Sampled inputs contain Output, MotionVectors, Depth and render dimensions892x909; native GetPrivateData consistently returns0x887a0002 with size0 and no pointer. Raw modes change0/2/3 with user controls, proving those choices reach the addon. The latest module check found no running TOW2 process; it verified the on-disk backend hash, not live module paths.

Added `AddonCommandListLifetime.hpp` as an isolated ownership component, NOT a ReShade command-list ABI and not wired into the backend. It canonicalizes native COM identity, retains each native list/device, isolates private data, bounds retained lists, pairs initialization/destruction callbacks, refuses recursive initialization, and stops new acquisition before refusing teardown with active leases. Callers must stop/retry before addon/device destruction; destroying a registry with active leases is a programming error and terminates. Foreign callback SEH guards remain the adapter's responsibility. No RenoDX tag is published by this component.

The Windows executable `build/addon-command-list-test/test-addon-command-list.exe` builds from `scripts/test-addon-command-list.vcxproj`. WARP checks passed distinct/repeated identity, concurrent acquisition, private-data isolation/deletion, native retention after external release, capacity/null rejection, failed-init rollback, tag removal and idempotent teardown. Tests use a separate test GUID and simulated callbacks, NOT RenoDX. Foreign-device rejection was skipped because repeated WARP creation returned the same device. No NR or game-GPU correctness claim.

Downstream audit prevents activating only the missing tag: source evaluation0x6c4f0 uses command-list slot4 for barriers (e.g. helper0x6f310), slot22 for resource copy (0x6d217), and pipeline/state restoration through0x6f390. Current fake command-list GPU slots remain stubs. Enabling the native tag alone would let the addon advance its internal resource states without recording the corresponding GPU operations. Next work is a complete scoped command-list adapter, resource operations and state restoration, plus actual addon lifecycle dispatch and reset synchronization. Do not deploy the lifetime component as an NR fix or ask for another unchanged game test.

No backend rebuild/deployment, addon replacement, game settings or registry changes in this stage. Deployed backend remains `E36583BF8A8C442EAC22F401AC339CDA8351C3E3D57413D26037F4082A017C43`.

### Corrected NGX callback diagnostics — September 9, 15:43

Correction: the earlier fe120/fe3ed probes cover a separate processing path, not the NGX post-evaluation callback. The 08:45 zero counters do not establish that all NR processing was unentered. Static tracing follows reconstruction wrapper RVA0x66fd0 to post-callback RVA0x869f0. Both saved GTA and TOW2 dumps contain two post-callback entries; their heap target pages were omitted, so unreadable pointers are not null callbacks.

The actual callback rejects unsuccessful reconstruction, null command list/parameters, recursion, disabled raw mode, absent Output/MotionVectors/Depth or zero DLSS.Render.Subrect.Dimensions.Width/Height. It then calls native command-list GetPrivateData with GUID `966aba18-ea54-4558-a09c-21ee51ca244b`, requiring success, eight bytes and a non-null ReShade command-list pointer. The addon itself supplies this tag at RVA0x96da0 (registered event2) and removes it during destruction. UEVR's temporary fake command list and separate private-data table do not provide this native tag or the required per-list lifecycle. This is a confirmed missing host contract, not yet an observed runtime rejection. Do not forge a tag pointing to the shared fake command list or bypass this check: persistent object identity, native binding, lifecycle and downstream GPU contracts must be implemented safely.

Replaced all five misplaced probes with four observation-only sites: entry0x86a16, raw mode0x86a83, inputs0x86d50, native GetPrivateData result0x86dc7. Exact PE identifier/image size and instruction bytes are checked before installation; partial installation rolls back the group. Stack reads are guarded. No inputs, registers, tags, rendering decisions or settings are altered. `DLSSNR-NGX-GATE` summaries report entry/mode/input/native-wrapper counts and wrapper rejections; counts are cumulative and not a synchronized snapshot.

Windows Release build and edited-source whitespace checks passed (existing D3D12Component narrowing warnings). No runtime probe or NR success claim. Deployed DLL/PDB with both copy hashes verified to Downloads/UEVR-AFW-JOEY while TOW2 was closed. Backend SHA256 `E36583BF8A8C442EAC22F401AC339CDA8351C3E3D57413D26037F4082A017C43`; rollback `pre-nr-status-20260909-154328-253`. Build uses the shared worktree. Game addon, registry and saved settings unchanged; NativeStereoFix and NativeStereoFixSamePass both false when checked.

Next test: launch TOW2, inject from that Downloads folder, enter gameplay with NR Auto enabled and keep Native Stereo Fix OFF. Leave it running for inspection. This test identifies the earliest blocking gate; it is not a performance-fix build. If native-wrapper lookup fails with valid inputs, next implementation is properly scoped command-list lifecycle and identity, with downstream contract review before enabling NR.

### Earlier NR-path probes — September 9, 08:44

The 08:39 run installed the validator-return probe but never reached it; reconstruction and Present delivery continued and addon status was Auto: Waiting. Added four fingerprint-checked midhooks in the same function: RVA0xfe177 (entry after prologue, before first feature-kind branch), 0xfe1f1 (after device/API checks), 0xfe398 (before computed zero-dimension mask test), and 0xfe6c1 (common epilogue). The previous validator-return probe remains. Entry diagnostics read raw kind, policy/mode bytes, two flags, five non-null object tests and three raw fields under SEH; names are offsets rather than invented semantic labels. No register/input/decision overrides. Exit counts include all normal paths, not just rejections or successes. Counters are cumulative and independently sampled, so concurrent calls can briefly differ.

Periodic Present summaries report all stage counts, including zero. Each added site's bytes are checked before installation; installation failure removes the early-probe group and logs failure. The addon PE identity checks remain in force. Build and whitespace checks passed; live-probe behavior remains unverified, and no NR fix is claimed.

Deployed backend/PDB with TOW2 closed, both copied hashes verified. Backend SHA256 `AA553ADF8AAD3AF3F3014188AF856EC2A3E448C037C10E4426FF980B2F8C6E0D`; rollback `pre-nr-status-20260909-084405-788` in Downloads/UEVR-AFW-JOEY. Settings unchanged. Next run: enter gameplay with NR Auto, Native Stereo Fix OFF; leave running about30seconds, then inspect `DLSSNR-EARLY` installation, entry snapshots and totals alongside `DLSSNR-ELIGIBILITY`.

### Source eligibility probe — September 9, 08:37

Deployed a diagnostic midhook immediately after the exact API18 addon's source-validator call at RVA0xfe3e8, observing its return at RVA0xfe3ed. Reads AL (accepted) and the rejection-string pointer at RBP+0x60 without changing registers, branch results or resources. Installation requires API18/name, PE reproducible-build identifier0xb6f86a12, image size0x279000 and matching 14 instruction bytes at the observation site. Unsupported fingerprints are refused. Logs first four calls and every600th as `DLSSNR-ELIGIBILITY`; absence of calls after confirmed installation means this checkpoint was not observed, not that validation succeeded. Earlier eligibility checks remain outside this probe.

Windows Release build passed; no in-game validation yet. Prior deployment attempt was blocked by a running game and changed nothing. After user closed TOW2, deployed backend/PDB to Downloads/UEVR-AFW-JOEY and verified both copied hashes. Backend SHA256 `A380C34F970672C928EF36AE981DE36F1EFA62B0C6D9255C011BC6551F009C79`; rollback `pre-nr-status-20260909-083757-573`. Saved settings, game addon and OpenXR registration unchanged. Next: enter gameplay with NR Auto enabled and Native Stereo Fix OFF, leave it running briefly, then inspect installation and eligibility lines. This probe is diagnostic, not an NR execution fix.

### Disabled controls — September 9, 07:58

Latest 07:52 log confirms addon-owned `Auto: Waiting`, `Off: Disabled`, and `Upscaled: Waiting`; no NR execution established. User reports controls disappear when presets are selected and remain editable with Off, unlike the real addon host. Verified missing ImGui19250 BeginDisabled/EndDisabled slots279/280; implemented paired scopes, including false scopes, and guarded callback cleanup that preserves host-owned scopes. This explains missing disabled styling/input blocking, but is NOT a proven fix for preset-dependent disappearance or absent NR evaluations.

Standalone regressions passed disabled alpha, nested false scope, cleanup and host-owned-scope preservation alongside prior tests. Windows Release passed with existing narrowing warnings. Deployed DLL/PDB to Downloads/UEVR-AFW-JOEY, game closed, both hashes verified. Backend SHA256 `02A5B1781A2950F75E36E2D97839F3752BC217FD599CBA4AF31303A22D311422`; rollback `pre-nr-status-20260909-075857-949`. No saved settings or addon binary changes. Remaining work: trace preset-dependent layout/control visibility independently of rendering prerequisites; do not present this partial UI correction as a completed NR fix.

### Word-wrapped tooltip bridge — September 9, 07:50

Recorded all 13 user screenshots in [the tooltip reference](RENODX_DLSS_TOOLTIPS.md). Implemented ImGui19250 slots218–222 with word wrapping and addon-owned tooltip cleanup. The existing addon text is forwarded, not replaced by hard-coded tooltip copies. Standalone UI tests passed multiline wrapping and host-window restoration, alongside existing regressions. Windows Release build passed with existing narrowing warnings; in-game hover has not yet been verified.

Deployed backend plus matching PDB to Downloads/UEVR-AFW-JOEY with TOW2 closed and hashes verified. DLL SHA256 `4F9DD8499DF2139E905D8F6FC990C7B83C547D9EA231F1EA250DE9E29457C147`; rollback `pre-nr-status-20260909-075009-528`. GTA installation, addon binaries, saved game settings and OpenXR registrations were not changed. Next test can combine reading RenoDX's own status with hovering a long help item (Encoding or Hook Method). No NR-execution fix is claimed.

### Addon-owned status display — September 9, 07:41

The 07:22 run delivered Present callbacks, initialized three backbuffers and intercepted reconstruction, but no NR evaluation was observed. Startup reads `DirectNeuralRenderingHookPoint=1`. CORRECTION: the earlier string-list inspection omitted Off; the user's screenshots show Off/Auto/Upscaled/Present. Numeric mapping remains to be verified, so this run must NOT be labeled Upscaled on that inference. The Present/DLSS-G warning alone does not prove the active blocking condition. Binary validation code rejects absent resource provenance and unmatched swapchain target dimensions; neither rejection has yet been captured as this run's actual reason. Preset2 switching also exposed ambiguous-section persistence warnings, separate from execution.

Found and restored missing ImGui19250 slots105–109. The addon's Waiting/Active status uses colored text slot105 (binary RVA0x40900, call through table offset0x348), formerly silently stubbed. Colored lines now render and are logged once per unique line (32-line bound) as `DLSSNR-ADDON-STATUS`. This is diagnostic visibility, not an NR processing fix. No resource-validation bypass or new GPU hook.

Standalone UI tests passed formatting, bounded diagnostic capture, text layout and existing style/font regressions. The first test run clipped an older geometry assertion after additional text; an explicit test-window size corrected the fixture and the rerun passed. Windows Release build passed with existing D3D12Component narrowing warnings. Deployed DLL/PDB to Downloads/UEVR-AFW-JOEY, both hashes verified with TOW2 closed. Backend SHA256 `F871E7ED724C936E539F6EDB1B209EF39C329A4C91E18E074618BA66CD7D2665`; rollback folder `pre-nr-status-20260909-074135-578`. No game addon, saved settings or OpenXR changes.

Next run: enter gameplay and open Neural rendering so the addon's own status callback executes; capture the newly visible status or inspect `DLSSNR-ADDON-STATUS` in the log. Keep Native Stereo Fix OFF. NR execution remains unresolved; do not request slider tuning as a substitute for evaluation evidence.

### Evaluation indicators — September 9, 07:15

The 07:05 run confirms the previous lifecycle patch initialized all three backbuffers and completed RenoDX init_swapchain. RenoDX reported its first intercepted D3D12 reconstruction evaluation. No NR evaluation was logged: DLL loading and attached hooks are not evidence of NR output or working overrides.

Added indicators to both DLSS pages. SR/AA/RR turns green after the addon reports an intercepted reconstruction evaluation this session; it is not a live activity counter or proof of a particular override. NR turns green only when its evaluation hook received a call within two seconds, amber for attached-but-zero or stale activity, and red for recorded refusal/lifecycle/Present faults. Counts measure calls, not successful output.

Release build passed (existing D3D12Component narrowing warnings). Deployed backend and matching PDB to `C:\Users\gthom\Downloads\UEVR-AFW-JOEY`, with game closed and both hashes checked. DLL SHA256 `165BC67F525F7591B15CA16368467C5776F8B5C04184C8EEAB7D0E3F387870BA`; rollback folder `pre-nr-status-20260909-071530-512`. This build includes the current shared worktree. No addon DLL, OpenXR registration, or saved game/stereo setting was changed by this deployment. Indicators have not yet been verified in-game; this is diagnostic visibility, not a rendering-effect fix.

User reports a stall after selecting Native Stereo Fix to address shimmering ice. Log shows view-hook installation at 07:08:22, followed by an in-process hang dump completed at 07:08:31. Preserved log/config and that fresh dump in profile `nr-hang-20260909-071003-334`. Game was already closed, so no additional live dump was captured. Timing alone does not establish the blocking call; dump stack analysis remains outstanding. Keep Native Stereo Fix OFF for this next diagnostic run; this is an isolation instruction, not a proven universal TOW2 incompatibility.

Next test: launch from the Downloads UEVR folder, enter gameplay, and capture both status lines on the DLSS page with NR enabled. Report ZERO versus an increasing NR call count, not just whether controls respond. Do not change Native Stereo Fix during this test. Successful NR output and SR override effects remain unproven.

UEVR source `src/mods/DlssNeuralRendering.cpp::load_addons_once` scans ALL `.addon64` files beside the executable, not the Steam game root. It stands down when a real ReShade is detected.

The 2026-09-08 14:03:00 profile log says real ReShade was loaded and the host stood down. ReShade.log identifies the proxy as `Arkansas/Binaries/Win64/d3d12.dll`. Disabling effects does not unload this proxy. Cheeky's addon was also loaded by ReShade in that run.

Requested new file: `F:\SteamLibrary\steamapps\common\TheOuterWorlds2\renodx-dlss.addon64`, SHA256 `fba3271626587f8b1f49c4fb40bbe23fd969282da6fe48be2da9b17beb708ee6`.

Test destination: `F:\SteamLibrary\steamapps\common\TheOuterWorlds2\Arkansas\Binaries\Win64\renodx-dlss.addon64`.

The existing `nvngx_dlssnr.dll` is present. The profile has UEVR NR host enabled, fixed foveation enabled, fraction 0.35, model resolution 75%, periphery refresh enabled, ring blend disabled. These are saved settings, not proof of a working runtime path or new addon compatibility.

## Test sequence and responsibilities

1. With TOW2 closed, copy the new addon beside the actual executable. Preserve the source. Temporarily rename the identified ReShade proxy and Cheeky addon with `.uevr-nr-test-off` suffixes; do not delete them. Leave old RenoDX `.off`/`.bak` files disabled. No registry, driver, or stereo-profile changes in this deployment.
2. Keep OFXR disarmed and Cheeky's OpenXR layer disabled for this isolated test. Fresh-launch TOW2 and inject the intended UEVR build. Keep the same scene, resolution, DLSS mode and stereo backend throughout comparisons. Do not combine OFXR with UEVR AFW during this test.
3. Leave the game open and notify the assistant. Assistant checks live modules (including `XRFrameBridge`, not only names containing `openxr`), loaded UEVR path/hash, addon registration/initialization, NR evaluation and crop diagnostics. User checks both eyes, rectangle placement, artifacts and responsiveness. Registration alone is not proof NR evaluated or improved performance.
4. Compare NR off, NR on without foveation, and NR on with the existing fixed rectangle, one change at a time in the same scene. Record application GPU/frame times as well as displayed FPS; generated frames are not equivalent to faster application rendering. Restart if an option only takes effect during initialization.
5. Stop at asymmetric eyes, a hang, or failed registration. Preserve logs before relaunch. Do not troubleshoot by changing several stereo or OpenXR controls at once.
6. Only after this isolated result, test OFXR separately from the stable baseline. Eye tracking and rounded masks are later experiments, not prerequisites for this measurement.

Rollback with game closed: rename the new deployed RenoDX addon out of `.addon64`, restore `d3d12.dll.uevr-nr-test-off` to `d3d12.dll` and `CheekyFoveatedDLSS.addon64.uevr-nr-test-off` to its original name. Never overwrite a destination that has since changed.

## Status

### Lifecycle candidate deployed — September 8, 19:31; verified September 9

Implemented the 48-byte resource-description ABI, with native descriptions for retained backbuffers and zero/unknown for unregistered handles. Added queue initialization, per-buffer resource initialization, then swapchain initialization for RenoDX DLSS API 18 only. Destruction is dispatched in reverse before releasing buffers. Callback faults disable further NR dispatch for the session. Buffer acquisition now waits for game-data initialization so Framework's mod-reset notification is available.

Windows Release build passed. The standalone Windows WARP test `scripts/test-addon-swapchain.vcxproj` passed actual backbuffer handles/count, invalid-handle rejection, the MSVC virtual struct-return calling convention, and release/ResizeBuffers/rebind with updated dimensions. This does NOT test third-party callback internals or prove successful neural rendering. Current color space remains unknown; non-backbuffer resources and other ReShade methods remain incompletely modeled.

Deployed matching DLL/PDB to `C:\Users\gthom\Downloads\UEVR-AFW-JOEY`. Backend SHA256 `60F8B438BC3F55C3C175948E3D3126CF42E2B69D61753BEBD845F145F2D87277`; rollback folder `pre-nr-lifecycle-20260908-193151-882`. Prior profile evidence: `nr-hang-20260908-193147-259`. September 9 inspection confirmed deployed hash unchanged and no newer run: log still ends September 8 at 19:19, before deployment.

Next user test: launch TOW2 using this injector, keep the existing isolated settings, enable NR using the addon controls and leave the game open for module/log verification. Agent must look for `DLSSNR-LIFECYCLE` initialization/fault status, addon init_swapchain completion, then actual NR evaluations and box applications. If the game stalls, capture the live hang instead of requesting another baseline test. No successful in-game result claimed yet.

### Earlier native swapchain stage — historical, superseded by candidate above

Added `AddonSwapchain.hpp` and corrected swapchain vtable slots 4–9. The host acquires actual D3D12 backbuffers with COM references, returns real count/index/window/handles, rejects out-of-range indices, translates supported color-space queries and releases snapshots during reset. Current color space remains explicitly unknown: DXGI has no getter and SetColorSpace1 is not tracked yet. The normal resize hook notifies Framework before native ResizeBuffers.

Windows Release compile passed and the edited source passed whitespace checks. No GPU/runtime lifecycle test performed. This stage does NOT dispatch the missing init_swapchain/resource events or supply resource descriptions, and is not an NR fix by itself. Before deployment, also verify teardown for early initialization (Framework only dispatches mod reset once game data is initialized), all resize paths, and callback/resource ordering. Do not deploy the current build output as though integration is complete.

The deployed backend remains the last widget build, SHA256 `485C3756C4B3DCF29DC6BB66D8B9188AC2B251B40959D88AD4FECBE61D0FFC5A`. Next engineering work is resource-description ABI and lifecycle dispatch with corresponding teardown tests; no user run requested for this incomplete stage.

### Rendering-path audit — September 8, after 19:14 run

RenoDX captured a live reconstruction evaluation at 19:14:57. NR snippet attached at 19:16:54 and UEVR installed Create/Evaluate detours, but no subsequent NR evaluation/foveal application was logged. Hook attachment is therefore established; successful NR rendering is not.

Found a definite swapchain ABI defect: actual virtual layout after device_object is 4=get_hwnd, 5=get_back_buffer, 6=get_back_buffer_count, 7=get_current_back_buffer_index, 8=check_color_space_support, 9=get_color_space. Our host incorrectly sets slot 5 to count=1 and slot 6 to index=0, leaving the other methods stubbed. Binary inspection of RenoDX init_swapchain confirms calls at offsets 0x30 (slot 6), 0x38 (slot 7), and 0x48 (slot 9). Thus its count query receives zero, and a buffer query would receive invalid resource 1.

Additionally, UEVR does not dispatch init_swapchain/resource lifecycle events. The binary contains a rejection path for output not matching a current swapchain target and missing resource provenance; those strings are NOT observed runtime rejection logs, so they must not be presented as the captured reason for this run.

Next implementation needs correct native-backed swapchain methods, retained backbuffer lifetimes, required resource descriptions/initialization, and paired resize/destruction callbacks before enabling the addon lifecycle. Do not simply send init_swapchain against the existing fake objects or disable resource validation. No rendering-path patch deployed by this audit.

### Segmented-widget bridge — September 8, 19:13

User screenshot shows the page drawing without an exception, but segmented choices lack text and do not respond usefully. Disassembly confirms those widgets call missing CalcTextSize (314), GetMousePos (338), and font-aware ImDrawList_AddText2 (395). Implemented all three, with the font handle translated back to a known native font rather than passing a newer font structure to native ImGui.

Scalar persistence now recognizes the modern schema and routes the six observed NR sliders to their exact requested `DirectNeuralRendering*` keys/sections. It refuses ambiguous preset sections and never falls back to old keys for the new addon. Custom segmented widgets bypass the standard slider/checkbox capture; their persistence remains unverified. Old stored settings are retained, not migrated or deleted. No complete persistence or NR functionality claim.

Standalone tests passed text geometry generation, text measurement and mouse coordinates alongside prior regressions. Windows Release build passed. Deployed DLL/PDB to Downloads/UEVR-AFW-JOEY; SHA256 `485C3756C4B3DCF29DC6BB66D8B9188AC2B251B40959D88AD4FECBE61D0FFC5A`. Rollback `pre-nr-widget-fix-20260908-191325-573`; prior profile evidence `nr-hang-20260908-191253-514`. Game was closed. Next test: labels visible, segment highlight follows selection, and slider changes logged under modern sections. Rendering effects and restart persistence are separate follow-up checks.

### Color bridge — September 8, 19:00 deployment

The 14:57 run progressed past the font fault and failed at addon RVA `0x94593`, dereferencing null returned by slot 69 (`GetStyleColorVec4`). Disassembly also showed color push/pop and RGB/HSV conversions in the surrounding code.

Implemented slots 49–51, 67–69 and 315–318 together: mapped color indices, a valid color reference, scoped color-stack restoration and the conversion helpers. Existing exception handling now restores addon-owned color pushes without consuming UEVR-owned pushes. Windows Release build and standalone bridge regressions passed, including nested color push/pop and restoration. These are host tests, not proof of a fully working addon page.

Deployed DLL/PDB to Downloads/UEVR-AFW-JOEY. Backend SHA256 `4E4E55D4563EDEEC8CE477E45A64E8DDC4E8A93B959749C3BA386979F8425D70`; rollback folder `pre-nr-color-fix-20260908-190017-014`. Previous fault evidence is in profile `nr-hang-20260908-150456-471`. Deployment guard confirmed game closed. Next: launch and open Neural rendering once, then inspect controls and/or the precise next exception. Rendering settings and OpenXR registration unchanged.

### Font bridge — September 8, 14:55

Style build progressed to a different fault at addon RVA `0x9528f`, read `0x48`. Disassembly shows `GetFont` (46), access to `LegacySize` (+0x1c) and read/write `Scale` (+0x48), followed by `PushFont` (44), `GetFontSize` (47), and font-pop restoration. Added `AddonFont19250.hpp` with explicit layout assertions and callback-local proxy handles. Only the observed LegacySize/Scale access is supported; newer atlas, baked font and glyph structures are NOT aliased to native objects. Push/pop translates scaling to native fonts and restores saved scales, including caught overlay faults. Other newer font APIs remain unsupported.

Windows Release build passed. Expanded standalone test passed native font-size scaling, nested scopes, interrupted cleanup, and unmatched-pop isolation, alongside previous style tests. This tests translation behavior, not complete addon UI compatibility.

Deployed matching DLL/PDB to Downloads/UEVR-AFW-JOEY; backend SHA256 `CDCB72E399B71BEE8E3C212CEC30BCB71A5DC5105A002923A8B148701DF4D20D`. Rollback folder `pre-nr-font-fix-20260908-145540-637`. Prior profile evidence `nr-hang-20260908-145536-583`. TOW2 was closed. Next test: open the Neural rendering page; report visible controls or the next fault. No changes to NR settings, addon binary or OpenXR registration.

### Style bridge fix — September 8, 14:48

The diagnostic run established an access violation in RenoDX at RVA `0xb0148`, reading `0x54`. Disassembly traces its null pointer to table slot 1, `GetStyle()`, not the last stub 289. The field is 1.92.5 `FrameRounding`.

Implemented `src/mods/AddonStyle19250.hpp`: separate 1.92.5-style snapshot, copied field-by-field from native 1.89.9, with explicit 60-color remapping and static assertions for FramePadding/FrameRounding offsets. Direct writes to the snapshot do not alter native style; this is not a complete bidirectional ImGui implementation. Added verified cursor, spacing, grouping, width, item-focus and item-rectangle forwarders used around the fault. The exception logger remains enabled.

Verification: Windows Release build passed. Standalone `scripts/test-addon-style.cpp` passed translation, color bounds/remapping, snapshot isolation and refresh assertions. This does not prove the third-party menu or NR rendering works in-game. Standalone test command from repository root:

```sh
g++ -std=c++17 -I dependencies/submodules/imgui scripts/test-addon-style.cpp dependencies/submodules/imgui/imgui.cpp dependencies/submodules/imgui/imgui_draw.cpp dependencies/submodules/imgui/imgui_tables.cpp dependencies/submodules/imgui/imgui_widgets.cpp -o build/test-addon-style
build/test-addon-style
```

Deployed backend and matching PDB to Downloads/UEVR-AFW-JOEY; DLL SHA256 `104AD9FD33ECD5C1C51FBFC0ECED7B0893830C1CEF20BEEA623DEB1B575F8AB2`. Previous pair: `pre-nr-style-fix-20260908-144804-315`. Prior run evidence: profile `nr-hang-20260908-144735-198`. Game was closed. No addon, OpenXR registry or game-setting changes. Next test is opening Neural rendering once to validate the known crash fix and identify any subsequent unsupported calls.

### Menu diagnostic build — September 8, 14:35

The 14:27 run reached gameplay and 600 Present callbacks. Opening the addon page faulted at 14:27:46, after which the host suppressed it. Last stub 289 maps to `IsItemFocused` in the official ReShade 19250 table; this is not proof it caused the exception. UEVR's linked ImGui is 1.89.9, so newer object layouts cannot safely be forwarded without translation.

Added `DLSSNR-UI-FAULT` logging of exception code, actual faulting address/module/offset, memory-access type and target. Release build passed; no menu fix claimed. Deployed backend SHA256 `0AF21244E99B5D3852C477807C6A61619A5260EDDB357F083E5B7CBFC4A57E3E` plus matching PDB to `C:\Users\gthom\Downloads\UEVR-AFW-JOEY`. Previous pair retained under `pre-nr-ui-diagnostic-20260908-143543-706` there. The build includes existing shared-worktree changes, not solely this diagnostic patch.

Previous profile evidence preserved in `nr-hang-20260908-143521-007`; game was closed, so no live dump was taken. Next run: inject this backend, open Neural rendering once, then inspect the new fault signature. Do not interpret absent UI as absent addon registration.

Deployment completed and hash verified using [the preparation script](../scripts/prepare-tow2-uevr-nr.ps1). No UEVR rebuild was performed.

The new-addon run at 14:14–14:15 froze after injection. User's repeated successful earlier launches establish the baseline; do not request another known-good run. The log confirms RenoDX DLSS API 18 registration, ImGui 19250, device initialization, return from the first Present callback, and successful OpenXR frame submission. It stops at 14:15:03. The existing September 7 dump is unrelated.

Confirmed host limitations, not proven hang causes:

- New settings use `RENODX-DLSS` / `RENODX-DLSS-preset1` and `DirectNeuralRendering*`. Old `RenoDX.DLSS5` / `NeuralUplift` settings were not requested in this run; the old fallback UI cannot be assumed to control this addon.
- The addon subscribes to lifecycle/rendering callbacks our partial host does not deliver. Execute-command-list dispatch was OFF; enabling it alone would not supply the missing lifecycle.
- The ImGui bridge still forwards only 32 functions. Menu and rendering compatibility require separate verification.

Next useful run must capture the failure, not repeat the baseline. Run [the capture script](../scripts/capture-tow2-nr-hang.ps1) with `-CaptureDump` while TOW2 is frozen. It preserves logs, inventories live modules including XRFrameBridge/OFXR, and captures a small diagnostic dump without terminating the game. Keep dumps local because they can contain process data. If the game is closed, it explicitly reports that no live capture was possible. No speculative host fix is deployed.

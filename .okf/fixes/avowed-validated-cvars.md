---
type: fix
title: Avowed register-tail CVar interface validation
description: Avowed integer CVar edits work, but one cascade and SamePass do not eliminate missing right-eye shadows; GPU capture investigation pending.
timestamp: 2026-09-28
tags: [avowed, cvars, validation, lighting]
---

# Evidence

September 27 12:48 run found the console registry but rejected 19 detected
performance entries as unsupported. VSM was saved as zero and the legacy path
logged its first freeze at 12:54, without verified readback. OFXR was reported
active by the user; it does not explain the static CVar interface rejection.

Installed executable disassembly at Release RVA `0x1b75dd0` proves:
`mov rax,[rcx]; mov rax,[rax]; mov edx,1; jmp rax`.
The old verifier recognized only direct dispatch through `[vtable+0]`.
The int-variable layout is Release15/Set16/Bool17/Int18/Float19, not TOW2's
extended-setter layout. GetBool includes a thread-local access path ending
in `setne al`; no candidate game function is executed during discovery.

# Change and safety

The bounded Release walk now tracks a destructor pointer loaded from exactly
vtable slot zero, allowing a register CALL/JMP only with retained provenance
and unchanged `this`. Wrong-slot and clobbered-register fixtures are rejected.
Avowed uses the validated standard/data/performance CVar and numeric script
paths, preserving its existing `0x08000000` setter priority rather than assuming
the TOW2/SHf priority. Writes report requested/before/readback and fail closed
on mismatch. Existing saved values and auto-script preference remain honored.

No OFXR, stereo-mode, same-pass, attachment, Lua, ReShade or game configuration
change is included. The September 27 instrumentation retirement stays intact.
The startup marker `[Avowed CVar] register-tail validation candidate 20260927`
identifies this candidate even when older generated build metadata is blank.

# Tests and live gate

Deployed/hash-verified September 27 13:15:30 to the shared
`C:/Users/gthom/Downloads/UEVR-AFW-JOEY` backend directory. Prior DLL/PDB have
adjacent `.pre-avowed-cvars-20260927-131530.bak` backups; logs/config snapshots
are in `diagnostics/avowed-cvars-20260927-131530`. No game was running.
DLL SHA-256: `df5309116263604049e97536ee3ab887bc57baefd3592f346bfa10c5afdb3dc8`.
PDB SHA-256: `9b95a80001d8db0a3f93e96da4092f7013a16c8975faaa078d6c12467b8d97c4`.

Release and CVar fixture tests passed: TOW2 extra-setter, older single-setter,
Avowed register-tail layout and integer/float readbacks, missing Release,
wrong destructor slot and clobbered destructor register rejection.
Fixtures execute only test-owned stubs, not game code.

Next run must show verified performance entries and VSM readback zero, then
confirm menu editing and stereo lighting visually. VSM is a candidate for the
image mismatch, not a proven Avowed diagnosis. If OFXR remains active, do not
attribute any remaining mismatch solely to the base renderer.

# September 27 live result and cascade candidate

17:11-17:14 log confirms requested/readback agreement for cascade counts
4/3/2/1, ShadowQuality, Lumen toggles, contact/distance-field shadows and VSM.
Initial user report suggested one cascade removed the eye mismatch. September
28 testing corrects that: one cascade does NOT eliminate missing right-eye
shadows. Multiple cascades can worsen it; VSM toggling does not resolve it.
This supersedes the earlier VSM hypothesis for Avowed, not the TOW2 finding.
Some float interfaces still report unavailable; do not claim all CVars work.

Current Avowed profile had NativeStereoFix=true, SamePass=false, Native
rendering. In `FFakeStereoRenderingHook.cpp`, `begin_render_viewfamily_real`
splits the two views into separate one-view renderer submissions. The
constructor hook only changes the secondary view to PRIMARY when SamePass
is enabled and the scene-capture target exists. Secondary-view classification
in a separate submission is a plausible cascade reuse/ownership problem,
NOT yet a proven renderer root cause. The recorded working Avowed profile
used SamePass=true. The existing Avowed frame-decrement safety guard remains.

With Avowed closed, changed only VR_NativeStereoFixSamePass to true on
September 27, after backing up config and log with suffix
`pre-cascade-samepass-20260927-173209`. Cascades remain 3, VSM remains 0.
No backend rebuild, AFW/OFXR/Cheeky change, or TOW2/SHf profile change.
September 28 gate FAILED: shadows remain left-only even at one cascade with
SamePass enabled. Neither setting is a confirmed fix. Restore the saved config
or set SamePass=false while closed to undo this candidate.

TOW2/SHf retained logs verify setters, not GPU consumption. TOW2 currently
saves Lumen.Reflections.Allow=0 alongside DownsampleFactor=3; a reflection
quality change is not evidence of a broken setter with reflections disabled.
Shadow controls likewise need the relevant rendering path and scene content.
Their full active-path audit remains open; no speculative global changes.

# September 28 source validation and next evidence

Latest run 18:01-18:05 confirms VSM 1->0 and ShadowQuality 5->3 readback;
cascade edits 1/2/3/4 also read back. Saved SamePass=true, NativeStereoFix=true,
RenderingMethod=0. The frame-decrement guard was active. Do not remove it:
the recorded reason is Avowed instability when rewinding the scene counter.

Disassembled the installed executable (cached constructor RVA 0x7afec90):
- At RVA 0x7aff09e, options +0x198/+0x19c copy to view +0x14a0/+0x14a4.
- Constructor compares +0x14a0 with secondary=2 and walks family views for
  primary association; +0x14a8 is assigned from that association/count.
- This supports the existing pass offset, but does NOT prove the runtime
  right eye's final classification or any shadow allocation/sampling result.

UEVR has no shadow-generation hook or captured shadow textures in the retained
log. Cannot distinguish missing shadow depth from failed projection/occlusion
or later composition without renderer evidence. No speculative offset write,
frame-counter edit, cache disable, or new rendering code was deployed.

Prepared `scripts/avowed-shadow-capture.ps1` using the existing AFW-PUBLISH
capture launcher and the currently installed backend via explicit paths.
Portable official RenderDoc goes only under diagnostics/renderdoc-1.46;
script verifies its signature, refuses a running game for launch, archives
profile/log, and supports -CheckOnly and -Capture. No global install or game
configuration changes. Game launch/capture and replay validation are pending.
A capture should show the same offending scene in both eye outputs, then trace
shadow depth, screen-space shadow mask and final lighting for the missing eye.
This is a correctness capture, not a performance benchmark.

Epic's FSceneView API distinguishes StereoPass, StereoViewIndex and
PrimaryViewIndex: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/FSceneView
Official RenderDoc download source: https://github.com/baldurk/renderdoc

# Related concepts

- [Avowed profile](../games/avowed.md)
- [Validated CVar access](tow2-validated-cvar-access.md)

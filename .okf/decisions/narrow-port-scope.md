---
type: decision
title: Port fixes narrowly — never merge broad subsystems between forks
description: When moving fixes between UEVR forks (regular ↔ PureDark AFW ↔ Joey), port only the executable-scoped compatibility blocks proven decisive; never copy lifecycle/renderer/UI subsystems. AFW→Native live mode switching is confirmed unsafe — restart instead.
tags:
- decision
- afw
- porting
- scope
timestamp: '2026-08-08T19:35:00+09:00'
---

# Decision

Cross-fork work (regular `<baseline-repo-root>` ↔ PureDark AFW
`<repo-root>`) ports **only** the narrow, executable-scoped
fix blocks documented as decisive in the source worklog. Explicitly excluded
from cross-fork ports: AFW lifecycle/restart gates, renderer or ghosting
implementations, VRS/foveated hooks, UI features, and rejected experiments.
Official PureDark runtime updates are a separate case: audit their small source
commits and ABI together, pin the replacement DLL by hash, stage an isolated
rollback, and port only the required compatibility delta rather than merging
the release branch.

**Why:** the TOW2 full-backend A/B swap imported the baseline's restart-gated
AFW lifecycle and known-bad artifact paths along with the fix under test — it
was immediately rolled back as "the wrong test". Conversely, porting *too
little* also failed: omitting the baseline's D3D12 Present instrumentation hid
the exact symptom it existed to observe. The rule is: port the documented fix
list *completely*, and nothing else.

**How to apply:** re-read the fix summary (`TOW2_UEVR_FIX_SUMMARY.md`-style)
before porting; check off every listed item; gate everything by executable
detection; deploy to the isolated directory with recorded hashes
([/playbooks/checkpoint-and-recovery.md](../playbooks/checkpoint-and-recovery.md)).

# Standing sub-decisions

- **Mode switching is directional.** Native → AFW has worked and is required
  for TOW2's safe Native-title startup. AFW → Native Stereo is confirmed unsafe
  (black right eye; PDAFW exposes no teardown API). Restart to leave AFW.
- **Treat the UESDK revision as a target-set checkpoint, not a universal
  upgrade.** Alpha.2's historical SHf candidate used `9034a857`; the current
  public branch used custom cache baseline `d9ee8a57`. Alpha.4 advances that
  branch through two narrow commits: upstream-equivalent FMalloc case handling
  and a small validated SceneView offset-publication API, preserving all other
  UESDK behavior. Revalidate every maintained game before changing the
  shared checkpoint. Preserve executable-scoped AddObject/FUObjectArray
  validation in backend code: validate RCX/RDX/R8/R9/stack candidates against
  the FUObjectArray index plus readable class/vtable before `add_new_object`,
  else skip ([render-target validation hardening](../fixes/render-target-validation-hardening.md)
  states the same validate-don't-guess principle).
- **Gate AFW work to active AFW frames.** PureDark ran AFW descriptor/texture/
  command-list setup every frame even in Native Stereo; gating it recovered
  baseline performance.
- **AFW frametime wins can be invisible in FPS buckets:** an ~8 ms saving kept
  both modes in SteamVR's 45 FPS half-rate bucket — measure frametime, not FPS.
- **Closed runtime + source update is one checkpoint.** Beta.4 changed both
  `PDAFWPlugin.dll` and its ABI/callers. Never hot-swap the DLL alone across an
  unverified header ABI. Preserve the prior runtime, backend and PDB together;
  see [the beta.4 correction](../fixes/afw-beta4-motion-vector-scale.md).

# Related

- [/projects/puredark-afw-integration.md](../projects/puredark-afw-integration.md) — the ongoing effort these rules govern

# Citations

- `PUREDARK_AFW_AVOWED_WORKLOG.md`, `PUREDARK_AFW_TOW2_WORKLOG.md`

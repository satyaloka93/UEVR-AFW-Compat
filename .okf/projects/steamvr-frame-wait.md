---
type: project
title: SteamVR frame-wait overlap experiment
description: "Retired TOW2 frame-wait experiment: higher submission rate but worse motion smoothness; code and option removed from deployed backend."
tags: [uevr, openxr, steamvr, tow2, pacing, experiment]
timestamp: '2026-09-27T08:22:44.8592410+09:00'
---

# Status

**Removed and deployed September27 08:54 at user request.** Higher FPS with
rougher moving/jumping failed the intended smoothness goal. Do not retain
inactive experimental options or automatically reintroduce this approach.

Removed menu/config registration, worker, deferred scheduling branches,
experiment-only telemetry and tests from active source. Restored original
synchronous xrWaitFrame and post-present begin behavior. Sync Mode remains in
VR > Debug. Earlier separate-queue experiment is also removed. Deleted obsolete
VR_SteamVRDeferredWait and VR_SteamVRRuntimeQueue keys from the TOW2 profile.
AFW itself and unrelated fixes remain. No commit or publication.

# Evidence

Sept27 08:49–08:50 AFW samples, Cheeky off:

| State | Submissions/s | Five-second range | Mean xrEndFrame CPU ms |
|---|---:|---:|---:|
| Overlap on |71.29|66.14–75.79|4.396|
| Overlap off |44.97|44.93–44.97|0.654|

Overlap caller wait rounded0.000ms, but user felt less smooth movement/jumping.
Five-second summaries cannot identify individual-frame pacing or pose mismatch.
One time-invalid/discard count followed each recreation; no settled submission
failures. Higher throughput is not sufficient evidence of improved VR delivery.
Earlier69.23/s result did not expose motion quality and must not override this
later report. SteamVR GPU timing also changes meaning when begin-frame moves
after engine rendering; do not interpret the low reported GPU time as a true
proportional speedup.

Evidence archives in OFXR workspace:
`build/run-analysis-20260927-tow2-overlap-motion`,
`build/run-analysis-20260927-tow2-frame-wait`.
Further pacing investigation should measure frame intervals and pose/display-time
association before proposing another scheduling change. No new experiment is
implemented or pending activation.

# Validation and installed artifact

VS2022 Release uevr target built successfully. Source search finds neither retired
option nor helper references in src/tests. OpenXR.cpp/.hpp restored to verified
pre-experiment source; whitespace check passed. Existing narrowing warnings
remain. Prior worker tests are historical, not tests of the restored scheduler.

Restored-scheduling checkpoint DLL SHA256: `329e209436b066707cc976f1f37f28560439a9aa429058b12ff42be133aad1fa`.
DLL/PDB verified against build output. Game closure checked before deployment.
Prior pair and original profile backup:
`F:/ai/GITHUB/UEVR-AFW-JOEY/UEVR-AFW-PUBLISH/build/retired-frame-wait-20260927/installed-backup-20260927-085454`.
Deployment record: `build/retired-frame-wait-20260927/deployment.json`.
Retired source/tests and build log: `build/retired-frame-wait-20260927`.

See [retired runtime queue](steamvr-runtime-queue.md) for the earlier experiment.

Current installed backend adds only [pacing diagnostics](tow2-pacing-diagnostics.md); the retired scheduling options remain removed.

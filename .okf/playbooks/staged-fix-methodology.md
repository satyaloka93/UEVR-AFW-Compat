---
type: playbook
title: Staged fix methodology — how every successful game fix was found
description: The working loop — evidence, hypothesis, one narrow executable-scoped change with telemetry, build+deploy with recorded hashes, test, interpret, keep or revert. Scope discipline and rejected-experiment records are as valuable as the fixes.
tags:
- methodology
- debugging
- process
timestamp: '2026-07-20T18:54:40+09:00'
---

# The loop

Every successful fix (TOW2 stall, Avowed recovery, Hogwarts crash) followed the
same shape, documented as numbered **stages** in a worklog:

1. **Problem summary + baseline evidence.** Capture the exact log, dump, and
   config before touching anything. Preserve incident artifacts with dated
   names (`log.puredark_afw_hang_20260718_192113.txt`).
2. **Working hypothesis.** Written down, so later stages can say "updated
   interpretation" honestly.
3. **One narrow change per stage, plus telemetry to observe it.** Often the
   stage's *telemetry* (Present heartbeat, end-frame results, swapchain slot
   logging) mattered more than the stage's fix — e.g. TOW2 stages 2–3 hooks
   never fired, which *was* the finding.
4. **Executable-scoped gating.** Fixes are guarded by game detection
   (`is_hogwarts_legacy_executable()`, TOW2 checks) so other titles keep
   original behaviour. Generalize only after multiple games prove the pattern.
5. **Build, deploy to an isolated directory, record SHA-256** of every deployed
   binary. Never touch the known-good primary install during experiments.
6. **Test with explicit success criteria written before the run.**
7. **Interpret and either keep or revert.** Record rejections with reasons —
   they prevent re-treading (see the rejected lists in
   [/games/outer-worlds-2.md](/games/outer-worlds-2.md) and
   [/games/hogwarts-legacy.md](/games/hogwarts-legacy.md)).

# Isolation techniques that paid off

- **Binary suspect elimination:** rename a plugin (`wandpos.dll.disabled`),
  reproduce the crash without it → deprioritize it. Restore afterwards.
- **A/B against known-good binaries** — but scope the A/B correctly: the AFW
  full-backend A/B was rolled back because it imported unrelated behaviour
  along with the fix under test ([/decisions/narrow-port-scope.md](/decisions/narrow-port-scope.md)).
- **One-variable CVar bisection** for visual issues
  ([/fixes/virtual-shadow-maps-stereo-mismatch.md](/fixes/virtual-shadow-maps-stereo-mismatch.md)).
- **Audit upstream as commits + ABI + runtime, not a branch merge.** The
  PureDark beta.4 update was reduced to Y motion scale, 2D guards and the
  matching official runtime; see
  [/fixes/afw-beta4-motion-vector-scale.md](/fixes/afw-beta4-motion-vector-scale.md).
- **Backup before every profile/INI change**, with dated `.bak` names.

# Failure-mode taxonomy (classify first)

- **Crash** (dump exists) → [/playbooks/crash-dump-analysis.md](/playbooks/crash-dump-analysis.md)
- **Stall** (no dump; Present/heartbeat stops while game threads continue) →
  [/playbooks/log-signature-triage.md](/playbooks/log-signature-triage.md)
- **Visual wrongness** (eye mismatch, ghosting) → CVar bisection, then
  [/playbooks/renderdoc-capture.md](/playbooks/renderdoc-capture.md)

Misclassifying a stall as a crash wastes a whole session — the TOW2 AFW "hang"
had continuous `XR_SUCCESS` telemetry right up to the moment Present stopped,
proving no crash occurred. But do not assume every identical-looking freeze has
the same cause: a later running-process dump found GameThread in unsafe UObject
class traversal. Capture thread state when the log alone cannot distinguish
render starvation from engine-thread failure.

# Before starting any of this

Snapshot the current working state:
[/playbooks/checkpoint-and-recovery.md](/playbooks/checkpoint-and-recovery.md).

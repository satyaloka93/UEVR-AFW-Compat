# Playbooks

Repeatable methods for analysing new UEVR game problems.

* [Basic 6DoF setup](basic-6dof-setup.md) — camera + weapon/hand attachment, native-UObjectHook-first then Lua; the motion-controller API, Lua sandbox limits, and the object-lifetime rules that keep a profile stable.
* [Staged fix methodology](staged-fix-methodology.md) — the core loop: evidence → hypothesis → one narrow scoped change + telemetry → test → keep/revert.
* [Log-signature triage](log-signature-triage.md) — symptom-to-fix table for `log.txt`; classify crash vs stall vs visual first.
* [Crash dump analysis](crash-dump-analysis.md) — minidump parsing and symbolization; the faulting module is not the culprit.
* [In-process hang dump](in-process-hang-dump.md) — capture elevated stalls when external ProcDump is denied.
* [RenderDoc capture](renderdoc-capture.md) — capture real stereo GPU frames via the UEVR-AFW-JOEY launcher.
* [Checkpoint and recovery](checkpoint-and-recovery.md) — sha256-pinned AppData profiles, dated backups, isolated deployments.
* [SH2 profile variant comparison](afw-profile-conversion.md) — scoped AFW/Native-era comparison and preservation method; uevr_utils migration is not a universal AFW requirement.
* [OKF maintenance](okf-maintenance.md) — amend canonical concepts in place and log concise bundle consumption/maintenance without turning OKF into a worklog.

---
type: playbook
title: Checkpointing and recovery — pin working states by hash
description: AppData profile files are outside git; pin known-good states with sha256, make dated backups before every change, deploy experiments to isolated directories, and record deployed-binary hashes. Restore by hash, not by filename convention.
tags:
- checkpoint
- backup
- recovery
- process
timestamp: '2026-07-20T18:54:40+09:00'
---

# Why

A working UEVR game state spans **three stores**: the repo (+ UESDK submodule),
the deployed binaries (`<baseline-deploy-dir>*`), and the untracked
AppData profile (`%APPDATA%\UnrealVRMod\<Game>\`). Git only protects the first.
Several regressions came from losing or mis-restoring the other two.

# Rules

1. **Pin AppData profiles by sha256.** Record hashes of `config.txt`,
   `user_script.txt`, `scripts/*.lua`, `data/*.txt` in a checkpoint doc (see
   `AVOWED_CHECKPOINT_STATE.md`). The Avowed known-good Lua is
   `f2b7df6d4f85c734…`.
2. **Restore by hash, never by filename convention.** Hard lesson:
   `Avowed6dof.lua.copy` was *not* the known-good file; restoring it caused a
   bad-recovery spiral. Verify the hash after restoring.
3. **Dated backups before every profile/INI change:**
   `config.txt.pre_native_test_20260320_111725.bak`,
   `Engine.ini.pre_puredark_afw_safe_vsm_20260718`.
4. **Isolated deployment for experiments.** Experimental builds go only to
   their own directory (`<deploy-dir>`); the primary
   known-good install is never overwritten mid-investigation.
5. **Record SHA-256 of every deployed binary** (`UEVRBackend.dll`, matching
   PDB, plugin DLLs, loaders) in the worklog. Treat a closed runtime DLL and its
   source header/caller ABI as one checkpoint; do not hot-swap only
   `PDAFWPlugin.dll`. This is what makes dump symbolization trustworthy
   ([/playbooks/crash-dump-analysis.md](crash-dump-analysis.md)) and
   A/B tests honest.
6. **Safety bundles before risky branch work:** copy the full displaced state
   (backend, PDB, profile, INI, last log) to a dated folder under
   `<evidence-root>/`; preserve working trees in git stashes
   with descriptive messages.
7. **Back up game saves** before testing experimental builds
   (`TOW2_saves_pre_afw_20260718` + sha256 manifest).
8. **Checkpoint commits in git** for repo + submodule at each recovered-working
   state, with the checkpoint doc excluded noise kept out of the commit.
9. **Preserve diagnostic-layout successes exactly.** Adding telemetry or a dump
   watchdog changes binary layout/timing and can make an intermittent race
   disappear. If a diagnostic build succeeds, save its backend/PDB/config/log
   before rebuilding; absence of a dump is evidence only that the stall did not
   occur in that run. See
   [/playbooks/in-process-hang-dump.md](in-process-hang-dump.md).

# Related

- [/playbooks/staged-fix-methodology.md](staged-fix-methodology.md)
- [/games/avowed.md](../games/avowed.md) — the profile this discipline protects

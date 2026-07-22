---
type: playbook
title: Windows minidump analysis for UEVR crashes
description: Parse the game's minidump, find UEVR modules on thread stacks, convert log addresses to module offsets, and symbolize against the matching local build PDB; a reusable script exists.
tags:
- crash
- minidump
- symbolization
- debugging
timestamp: '2026-07-20T18:54:40+09:00'
---

# Crash dump versus live-stall dump

This procedure applies to both exception-generated crash dumps and minidumps
captured from a frozen process. When no dump exists and external ProcDump is
denied, first use the executable-scoped
[in-process hang-dump playbook](in-process-hang-dump.md).

# Inputs to gather

- Game crash dump: e.g. `%LOCALAPPDATA%\<Game>\Saved\Crashes\<id>\*.dmp`
  and `CrashContext.runtime-xml` (gives crashed thread, exception code/address,
  faulting module).
- UEVR log: `%APPDATA%\UnrealVRMod\<GameExeName>\log.txt`.
- The **matching** `UEVRBackend.dll` + `UEVRBackend.pdb` from the local build
  (`<baseline-repo-root>/build/bin/uevr/`). Symbolization is only trustworthy if
  the deployed DLL exactly matches this build/PDB pair — record deploy hashes
  ([/playbooks/checkpoint-and-recovery.md](checkpoint-and-recovery.md)).

# Procedure

```bash
python <uevr-vr-dev-skill>/scripts/analyze_windows_uevr_dump.py /
  --dump "<path to .dmp>" \
  --symbolize-module-win "<baseline-repo-root>/build/bin/uevr/UEVRBackend.dll" /
  --game-pdb-win "<game pdb if available>" \
  --extra-offset 0x<offset> ...
```

The script parses the minidump, lists UEVR-related modules, scans thread stacks
for `UEVRBackend.dll` addresses, and symbolizes via `llvm-symbolizer.exe`.

**Converting log addresses:** runtime addresses in `log.txt` become offsets by
subtracting the module base found in the dump
(`0x7ff925b9fc74 - 0x7ff9254b0000 = 0x6efc74`), then feed them as
`--extra-offset`.

# Interpretation rules learned

- **The faulting module is not the culprit.** Hogwarts crashed inside
  `HogwartsLegacy.exe`, but the stack held `safetyhook::MidHook::create` and
  `memcpy` from UEVRBackend — the framework's hook-patching destabilized the
  game ([/games/hogwarts-legacy.md](../games/hogwarts-legacy.md)).
- Cross-reference the dump with the log's final minutes; the log usually names
  the fragile subsystem (repeated null-deref handler lines) before the crash.
- A crashing offset + PDB pins the exact source line: the AFW injection crash
  symbolized straight to `UObjectHook::add_new_object`
  (`src/mods/UObjectHook.cpp:469`), which dictated the fix.
- `src/ExceptionHandler.cpp` was extended to capture stack traces in the global
  exception handler — check the UEVR log for those before reaching for the dump.
- A TOW2 live-stall dump put GameThread in `UObjectHook::add_new_object()` while
  traversing `UStruct::get_super_struct()`. That proved a per-call AddObject
  register-layout change, not a Present/GPU deadlock; see
  [/fixes/tow2-addobject-candidate-guard.md](../fixes/tow2-addobject-candidate-guard.md).

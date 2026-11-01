---
type: playbook
title: Finding the real UObject creation hooks in a shipped UE game
description: Static method (REA or capstone/pefile) to verify UEVR's AddObject hook and locate StaticAllocateObject, StaticConstructObject_Internal and AllocateUObjectIndex via the engine's slot-check string, unwind chains and argument checks; reproduced by tools/uobject-creation-re.
tags: [uobjecthook, reverse-engineering, rea, avowed, tow2, attachments, reusable]
timestamp: 2026-11-01
---

Use this when a game shows the [guarded AddObject symptoms](../fixes/guarded-addobject-object-recovery.md):
`Skipping AddObject call with no validated FUObjectArray-backed candidate`, the guard
"discovering" different registers/stack slots (`stack1`..`stack4`, `R9`), late or missing
Lua attachments, or large `FUObjectArray recovery scan adopted` totals. Avowed and TOW2
are solved; this is how to repeat it for another game or a game patch.

# Quick check

```bash
pip install pefile capstone
python tools/uobject-creation-re/find_creation_functions.py "<exe>" --hooked <rva>
```

`<rva>` = UEVR log `[UObjectBase] AddObject: 0x...` minus `Game Module Addr`.
Expected: 2-4 creation functions, each with `args<=8 [OK]`. Then confirm in game that
the log shows `Hooked object creation function at RVA ...` (one line per function), no
`Skipping AddObject` warnings, and near-zero recovery-scan adoptions after a level load.

# Method

All steps are static: only the exe on disk is read; nothing is executed.

1. **Inputs from UEVR `log.txt`.** `Game Module Addr`, `[UObjectBase] AddObject: 0x...`,
   `Found GUObjectArray at 0x...` and `FUObjectItem distance`. Subtract the module base to
   get RVAs (on-disk VA = ImageBase + RVA).
2. **Check what the heuristic hooked.** Disassemble the AddObject RVA. A real
   `UObjectBase::AddObject` receives the object in `rcx`, writes the FName at
   `[rcx+0x18]` (UE5 layout), calls AllocateUObjectIndex with the object in `rdx` and
   ends in HashObject. Found instead: TOW2 `0x102F414` takes (map, hash, key) and walks
   32-byte buckets (a hash-map FindOrAdd whose only caller is StaticAllocateObject);
   Avowed `0x7B0D8B0` compares count/capacity at `[rcx+8]`/`[rcx+0xC]` (an array shrink
   helper with 252 callers). Cause: AddObject is inlined into StaticAllocateObject, so
   UESDK's `get_add_object` substitutes a nearby call.
3. **Anchor on the slot check.** Every slot assignment carries the fatal check
   `"Attempting to add %s at index %d but another object (0x%016llx) exists at that index!"`
   (wide string). It exists in both the standalone `FUObjectArray::AllocateUObjectIndex`
   and in copies inlined into StaticAllocateObject / StaticConstructObject_Internal.
   Avoid anchoring on `GUObjectArray` itself: few functions load it with `lea`, and
   `NumElements` (`+0x24`) has ~50k read sites and no atomic increment.
4. **Find references.** TOW2 references the string with RIP-relative `lea`. UE 5.2+
   UE_LOG uses static log records: Avowed's code references a record that holds the
   string's absolute address (search the image for the 8-byte pointer, then for
   RIP-relative references to the record).
5. **Walk to the function entry.** References sit in outlined cold blocks. Follow
   `.pdata` chained unwind info (UNW_FLAG_CHAININFO) to the primary entry (kananlib
   `find_function_start_unwind` at runtime). TOW2 StaticAllocateObject is split into
   tiny chunks (`0x102D9D2 -> 0x102D9C4 -> 0x102D9A0`), so chunk size cannot classify roles.
6. **Identify roles.**
   - StaticAllocateObject: ~65 callers; reads `UClass` size/alignment
     (`[rcx+0x58]`/`[rcx+0x5C]`), calls the find-existing-object routine, returns the new
     object (`mov rax, rdi; ... ret`) with index, class, outer and name set.
   - StaticConstructObject_Internal (Avowed only): huge, 2 callers, strings such as
     `NewObject with empty name can't be used to create default subobjects`; returns the object.
   - AllocateUObjectIndex: 1 caller (the real non-inlined AddObject); enters the array
     critical section (`[rcx+0x30]` or a RIP global), keeps `rdx` as the object and writes
     it into the slot (`mov [slot], reg`).
7. **Check the detour can forward the arguments.** UObjectHook forwards rcx, rdx, r8, r9
   and four stack slots. Find the highest stack argument read relative to the entry rsp
   (frame = `sub rsp` + pushes; frame-pointer functions use `lea rbp, [rax - N]` after
   `mov rax, rsp`). Measured: StaticAllocateObject 8, AllocateUObjectIndex 5,
   StaticConstructObject_Internal 4 (Avowed). UObjectHook repeats this measurement at startup and skips any root reading more than 8 arguments (or with an unknown frame).

# How UEVR uses it

`UObjectHook::hook_creation_functions()` repeats steps 3-5 at startup on guarded games and
hooks up to four roots with one detour that adopts the return value and `rdx` when each is
an untracked object passing FUObjectArray slot + class-chain validation. Already-tracked
candidates (e.g. the Outer in StaticAllocateObject's `rdx`) are skipped, so no per-function
role is needed. If discovery fails the heuristic AddObject hook is used.

# Using REA

The REA MCP route is `execute_rizin_command` on the exe (e.g. `iz~exists at that index`,
`axt @ <string>`, `pd 60 @ <va>`), which needs the Rizin CLI installed; it returned
`ENOENT` on this machine, so the Avowed/TOW2 analysis used capstone + pefile directly
(the tool above). A full Ghidra `open_binary` analysis of these 180-195 MB executables is
impractical; prefer targeted disassembly at known RVAs. Record which provider produced
each conclusion, as for any static evidence.

# Limits

Static analysis identifies candidates; the in-game log checks above are the acceptance
test. The string and the record layout are engine-version dependent. Builds without the
string or with more than four roots fall back to the heuristic hook and recovery scan.

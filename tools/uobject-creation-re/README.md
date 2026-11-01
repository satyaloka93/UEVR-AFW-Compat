# UObject creation-function finder

`find_creation_functions.py` statically locates the functions UEVR's UObjectHook hooks on
guarded games (Avowed, TOW2) to track new UObjects: StaticAllocateObject,
StaticConstructObject_Internal and FUObjectArray::AllocateUObjectIndex. It mirrors the
runtime discovery in `UObjectHook::hook_creation_functions()`, so a new game or patch can
be checked before an in-game test. Nothing is executed; only the exe on disk is read.

```bash
pip install pefile capstone
python find_creation_functions.py "<game>/Binaries/Win64/<Game>-Win64-Shipping.exe" --hooked <rva>
```

`--hooked` is optional: the RVA UEVR hooked as AddObject (`AddObject: 0x...` minus
`Game Module Addr` in UEVR's `log.txt`). The report lists each creation function's
RVA, direct callers, stack arguments read (must be <= 8 for the detour) and its first
instructions, and shows what the heuristic AddObject actually is.

Method and interpretation: `.okf/playbooks/uobject-creation-hook-discovery.md`.

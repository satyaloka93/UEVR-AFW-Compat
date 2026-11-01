#!/usr/bin/env python3
"""Locate the UObject creation functions UEVR should hook in a shipped UE executable.

Mirrors UObjectHook::hook_creation_functions() offline so a new game or patch can be
checked before testing in-game:

  1. find the wide string "...exists at that index!" (FUObjectArray slot check),
  2. collect RIP-relative references to it, and references to any static UE_LOG
     record that holds its absolute address (UE 5.2+ log records),
  3. walk .pdata/unwind chains from each reference to the enclosing function entry,
  4. report direct callers, stack arguments read, and the first instructions.

Optionally pass --hooked RVA (UEVR log "AddObject: 0x..." minus "Game Module Addr")
to see what the heuristic AddObject hook actually is.

Requires: pip install pefile capstone   (static analysis only; nothing is executed)
"""
import argparse, bisect, re, sys
import pefile, capstone

SLOT_CHECK = "exists at that index!"
MAX_DETOUR_ARGS = 8  # UObjectHook's detour forwards rcx, rdx, r8, r9 + 4 stack slots


def load(path):
    pe = pefile.PE(path, fast_load=True)
    pe.parse_data_directories(directories=[pefile.DIRECTORY_ENTRY["IMAGE_DIRECTORY_ENTRY_EXCEPTION"]])
    raw = open(path, "rb").read()
    text = [(s.VirtualAddress, raw[s.PointerToRawData:s.PointerToRawData + s.SizeOfRawData])
            for s in pe.sections if s.Characteristics & 0x20000000]
    entries = sorted(pe.DIRECTORY_ENTRY_EXCEPTION, key=lambda e: e.struct.BeginAddress)
    return pe, raw, text, entries


def function_root(entries, begins, rva):
    """Follow chained unwind info (UNW_FLAG_CHAININFO) to the primary function entry."""
    i = bisect.bisect_right(begins, rva) - 1
    if i < 0 or not (entries[i].struct.BeginAddress <= rva < entries[i].struct.EndAddress):
        return None
    e = entries[i]
    for _ in range(16):
        unwind = getattr(e, "unwindinfo", None)
        parent = getattr(unwind, "FunctionEntry", None) if unwind else None
        if parent is None:
            break
        b = parent if isinstance(parent, int) else getattr(parent, "BeginAddress", None) or parent.struct.BeginAddress
        e = entries[bisect.bisect_right(begins, b) - 1]
    return e.struct.BeginAddress


def rip_refs(text, target):
    """RIP-relative LEA/MOV references (REX.W 8D/8B with disp32) resolving to target."""
    out = []
    for base, data in text:
        for m in re.finditer(rb"[\x48\x4c][\x8d\x8b][\x05\x0d\x15\x1d\x25\x2d\x35\x3d]", data):
            o = m.start()
            if base + o + 7 + int.from_bytes(data[o + 3:o + 7], "little", signed=True) == target:
                out.append(base + o)
    return out


def direct_callers(text, targets):
    found = {t: [] for t in targets}
    for base, data in text:
        for m in re.finditer(rb"\xe8", data):
            o = m.start()
            t = base + o + 5 + int.from_bytes(data[o + 1:o + 5], "little", signed=True)
            if t in found:
                found[t].append(base + o)
    return found


def stack_args_read(pe, raw, md, rva, length=0x10000):
    """Highest stack argument index read, from rsp- or frame-pointer-relative accesses."""
    off = pe.get_offset_from_rva(rva)
    insns = list(md.disasm(raw[off:off + length], rva))
    frame, rbp_delta, pushes = None, None, 0
    for x in insns[:40]:
        if x.mnemonic == "mov" and x.op_str == "rax, rsp":
            rbp_delta = 0  # entry rsp captured in rax; later "lea rbp, [rax - N]"
        elif x.mnemonic == "lea" and x.op_str.startswith("rbp, [rax - ") and rbp_delta == 0:
            rbp_delta = -int(x.op_str.split("- ")[1].rstrip("]"), 16)
        elif x.mnemonic == "push":
            pushes += 1
        elif x.mnemonic == "sub" and x.op_str.startswith("rsp, "):
            frame = int(x.op_str.split(", ")[1], 16) + pushes * 8
            break
    args = set()
    for x in insns:
        if x.mnemonic == "int3" and x.address > rva + 0x40:
            break
        for reg, off_s in re.findall(r"\[(rsp|rbp) \+ (0x[0-9a-f]+)\]", x.op_str):
            v = int(off_s, 16)
            if reg == "rsp" and frame is not None:
                k = v - frame - 0x28
            elif reg == "rbp" and rbp_delta is not None:
                k = v + rbp_delta - 0x28
            else:
                continue
            if k >= 0 and k % 8 == 0:
                args.add(5 + k // 8)
    return max(args) if args else 4


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("exe")
    ap.add_argument("--hooked", help="RVA UEVR hooked as AddObject (hex)")
    ap.add_argument("--asm", type=int, default=12, help="instructions to print per function")
    a = ap.parse_args()

    pe, raw, text, entries = load(a.exe)
    begins = [e.struct.BeginAddress for e in entries]
    base = pe.OPTIONAL_HEADER.ImageBase
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_64)

    off = raw.find(SLOT_CHECK.encode("utf-16-le"))
    if off < 0:
        sys.exit("slot-check string not found: UEVR will fall back to the heuristic AddObject")
    while raw[off - 2:off] != b"\x00\x00":
        off -= 2
    srva = pe.get_rva_from_offset(off)
    print(f"slot-check string at RVA {srva:#x}: {raw[off:off+200].decode('utf-16-le').split(chr(0))[0]!r}")

    refs = [(r, "direct") for r in rip_refs(text, srva)]
    ptr = (base + srva).to_bytes(8, "little")
    for m in re.finditer(re.escape(ptr), raw):
        rec = pe.get_rva_from_offset(m.start())
        refs += [(r, f"via UE_LOG record {rec:#x}") for r in rip_refs(text, rec)]
    if not refs:
        sys.exit("no code references: UEVR will fall back to the heuristic AddObject")

    roots = {}
    for r, how in refs:
        root = function_root(entries, begins, r)
        if root is not None:
            roots.setdefault(root, []).append(f"{r:#x} ({how})")
    calls = direct_callers(text, list(roots) + ([int(a.hooked, 16)] if a.hooked else []))

    print(f"\n{len(roots)} creation function(s) UEVR will hook (max 4):")
    for root, why in roots.items():
        nargs = stack_args_read(pe, raw, md, root)
        ok = "OK" if nargs <= MAX_DETOUR_ARGS else "TOO MANY ARGS FOR DETOUR"
        print(f"\n  RVA {root:#x}  callers={len(calls[root])}  args<={nargs} [{ok}]  refs: {', '.join(why[:2])}")
        o = pe.get_offset_from_rva(root)
        for i, x in enumerate(md.disasm(raw[o:o + 200], root)):
            if i >= a.asm:
                break
            print(f"      {x.address:08x}: {x.mnemonic} {x.op_str}")

    if a.hooked:
        h = int(a.hooked, 16)
        print(f"\nheuristic AddObject RVA {h:#x}: {len(calls[h])} direct callers; caller functions: "
              f"{sorted({hex(function_root(entries, begins, c) or 0) for c in calls[h]})[:6]}")
        o = pe.get_offset_from_rva(h)
        for i, x in enumerate(md.disasm(raw[o:o + 200], h)):
            if i >= a.asm:
                break
            print(f"      {x.address:08x}: {x.mnemonic} {x.op_str}")


if __name__ == "__main__":
    main()

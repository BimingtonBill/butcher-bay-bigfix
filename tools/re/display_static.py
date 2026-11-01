"""Read-only x64 PE inspection. Run with local/venv/Scripts/python.exe -I.

Outputs disassembly to stdout; keep full output in ignored local/, not git.
No target code is executed. Addresses in output are module-relative RVAs.
"""
import argparse
import bisect
import hashlib
import struct
from pathlib import Path

import capstone as cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP
import pefile


APIS = ("EnumDisplaySettings", "ChangeDisplaySettings", "CreateWindowEx",
        "SetWindowLong", "GetWindowLong", "SetWindowPos", "ShowWindow", "RegisterClass",
        "DefWindowProc", "GetProcAddress", "LoadLibrary", "SetFocus",
        "SetForegroundWindow", "DestroyWindow", "AdjustWindowRect")
MODULES = ("System/Win64_AMD64/SbzEngine.exe", "System/Win64_AMD64/MSystem.dll",
           "System/Win64_AMD64/RndrGL.dll", "System/Win64_AMD64/MXR.dll",
           "System/Win64_AMD64/MCCDyn.dll", "Content/GameClasses_Win64_AMD64.dll",
           "Content/GameWorld_Win64_AMD64.dll")


class Image:
    def __init__(self, path):
        self.path = Path(path)
        self.raw = self.path.read_bytes()
        self.pe = pefile.PE(data=self.raw)
        if self.pe.FILE_HEADER.Machine != 0x8664:
            raise ValueError(f"Expected AMD64 PE: {path}")
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.data = self.pe.get_memory_mapped_image()
        self.md = cs.Cs(cs.CS_ARCH_X86, cs.CS_MODE_64)
        self.md.detail = True
        self.md.skipdata = True
        self.imports = {}
        for entry in getattr(self.pe, "DIRECTORY_ENTRY_IMPORT", []):
            for imp in entry.imports:
                self.imports[imp.address - self.base] = (
                    entry.dll.decode() + "!" +
                    (imp.name.decode() if imp.name else f"ordinal:{imp.ordinal}"))
        self.functions = sorted((e.struct.BeginAddress, e.struct.EndAddress)
                                for e in getattr(self.pe, "DIRECTORY_ENTRY_EXCEPTION", []))
        self.starts = [f[0] for f in self.functions]
    def instructions(self):
        for section in self.pe.sections:
            if section.Characteristics & 0x20000000:
                start = section.VirtualAddress
                size = min(section.Misc_VirtualSize, section.SizeOfRawData)
                yield from self.md.disasm(self.data[start:start + size], start)

    def function(self, rva):
        i = bisect.bisect_right(self.starts, rva) - 1
        if i >= 0 and rva < self.functions[i][1]:
            return self.functions[i]
        return None

    def references(self, insn):
        if not insn.id:
            return []
        refs = []
        for op in insn.operands:
            if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                refs.append(insn.address + insn.size + op.mem.disp)
            elif op.type == X86_OP_IMM:
                refs.append(op.imm)
        return refs

    def line(self, insn):
        labels = [self.imports[r] for r in self.references(insn) if r in self.imports]
        # Capstone's RIP operands display displacements; annotate resolved RVA.
        targets = []
        if insn.id:
            for op in insn.operands:
                if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                    targets.append(f"RVA 0x{insn.address + insn.size + op.mem.disp:x}")
        return f"{insn.address:08x}  {insn.mnemonic:8} {insn.op_str}" + (
            " ; " + ", ".join(labels + targets) if labels or targets else "")

    def dump(self, start, end):
        for insn in self.md.disasm(self.data[start:end], start):
            print(self.line(insn))

    def raw_import_calls(self, targets):
        """Independent byte sweep for FF /2 and /4 RIP-relative IAT calls/jumps.

        This checks linear-disassembly coverage, not runtime reachability or
        computed function pointers. Each candidate is decoded before acceptance.
        """
        found = set()
        for section in self.pe.sections:
            if not section.Characteristics & 0x20000000:
                continue
            start = section.VirtualAddress
            end = start + min(section.Misc_VirtualSize, section.SizeOfRawData)
            for rva in range(start, end - 5):
                if self.data[rva:rva + 2] not in (b"\xff\x15", b"\xff\x25"):
                    continue
                target = rva + 6 + struct.unpack_from("<i", self.data, rva + 2)[0]
                if target in targets:
                    insn = next(self.md.disasm(self.data[rva:rva + 6], rva))
                    if insn.mnemonic in ("call", "jmp"):
                        found.add(rva)
        return found


def number(value):
    return int(value, 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path)
    parser.add_argument("--module", type=Path)
    parser.add_argument("--at", type=number, action="append", default=[],
                        help="Dump containing unwind range (repeatable; split functions may have several)")
    parser.add_argument("--range", nargs=2, type=number, metavar=("START", "END"))
    parser.add_argument("--xref", type=number, action="append", default=[],
                        help="Code and absolute data-pointer references to an RVA (repeatable)")
    parser.add_argument("--string", action="append", default=[])
    parser.add_argument("--export", action="append", default=[],
                        help="List exports containing text")
    parser.add_argument("--check-iat", action="store_true",
                        help="Compare IAT call/jump coverage with an independent byte sweep")
    args = parser.parse_args()
    if not args.root and not args.module:
        parser.error("--root or --module required")
    paths = [args.module] if args.module else [args.root / p for p in MODULES]
    for path in paths:
        im = Image(path)
        print(f"\n{path.name} base=0x{im.base:x} sha256={hashlib.sha256(im.raw).hexdigest()}")
        for symbol in getattr(getattr(im.pe, "DIRECTORY_ENTRY_EXPORT", None), "symbols", []):
            name = symbol.name.decode() if symbol.name else ""
            if any(s in name for s in args.export):
                print(f"export RVA 0x{symbol.address:x}: {name}")
        if args.at:
            for rva in args.at:
                bounds = im.function(rva)
                print("unwind range:", tuple(hex(x) for x in bounds) if bounds else None)
                if bounds:
                    im.dump(*bounds)
        elif args.range:
            im.dump(*args.range)
        else:
            targets = {r: s for r, s in im.imports.items() if s.split("!")[1].startswith(APIS)}
            if args.xref:
                targets = {r: "requested xref" for r in args.xref}
                for rva in args.xref:
                    needle = struct.pack("<Q", im.base + rva)
                    offset = 0
                    while (offset := im.data.find(needle, offset)) >= 0:
                        print(f"absolute pointer at RVA 0x{offset:x} -> RVA 0x{rva:x}")
                        offset += 8
            for s in args.string:
                for encoding in ("ascii", "utf-16le"):
                    needle = s.encode(encoding)
                    offset = 0
                    while (offset := im.data.find(needle, offset)) >= 0:
                        print(f"string {s!r} {encoding} RVA 0x{offset:x}")
                        targets[offset] = s
                        offset += len(needle)
            for r, name in targets.items():
                print(f"target 0x{r:x}: {name}")
            calls = set()
            for insn in im.instructions():
                hits = [targets[r] for r in im.references(insn) if r in targets]
                if hits:
                    bounds = im.function(insn.address)
                    label = tuple(hex(x) for x in bounds) if bounds else None
                    print(f"unwind={label} {im.line(insn)} => {', '.join(hits)}")
                    if insn.mnemonic in ("call", "jmp") and any(
                        r in im.imports and r in targets for r in im.references(insn)
                    ):
                        calls.add(insn.address)
            if args.check_iat:
                raw_calls = im.raw_import_calls(targets.keys() & im.imports.keys())
                if calls != raw_calls:
                    raise RuntimeError(f"IAT coverage mismatch: decoded={calls}, byte sweep={raw_calls}")
                print(f"IAT coverage PASS: {len(calls)} call/jump sites agree with byte sweep")


if __name__ == "__main__":
    main()

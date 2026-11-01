"""Read-only x86 SSE2 / AMD64 bloom investigation. Never loads game code.

Run with local/venv/Scripts/python.exe -I. Addresses are RVAs; x86 operands
retain preferred VAs (annotated references are converted to RVAs).
"""
import argparse
import hashlib
import re
from pathlib import Path

import capstone as cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP
import pefile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", type=Path)
    parser.add_argument("--string", action="append", default=[])
    parser.add_argument("--xref", type=lambda s: int(s, 0), action="append", default=[])
    parser.add_argument("--range", nargs=2, type=lambda s: int(s, 0))
    args = parser.parse_args()
    raw = args.module.read_bytes()
    pe = pefile.PE(data=raw)
    data = pe.get_memory_mapped_image()
    base = pe.OPTIONAL_HEADER.ImageBase
    x64 = pe.FILE_HEADER.Machine == 0x8664
    if not x64 and pe.FILE_HEADER.Machine != 0x14c:
        parser.error("expected x86 or AMD64 PE")
    print(args.module, "base=" + hex(base), "sha256=" + hashlib.sha256(raw).hexdigest())
    md = cs.Cs(cs.CS_ARCH_X86, cs.CS_MODE_64 if x64 else cs.CS_MODE_32)
    md.detail = True
    md.skipdata = True
    targets = set(args.xref)
    for match in re.finditer(rb"[ -~]{4,}", data):
        if any(s.lower() in match[0].decode().lower() for s in args.string):
            print("string", hex(match.start()), repr(match[0]))
            # Match substrings after a printable byte preceding the true string.
            for s in args.string:
                offset = match[0].lower().find(s.lower().encode())
                if offset >= 0:
                    targets.add(match.start() + offset)
    ranges = [args.range] if args.range else [
        (s.VirtualAddress, s.VirtualAddress + min(s.Misc_VirtualSize, s.SizeOfRawData))
        for s in pe.sections if s.Characteristics & 0x20000000]
    for start, end in ranges:
        for insn in md.disasm(data[start:end], start):
            refs = []
            for op in insn.operands if insn.id else []:
                if op.type == X86_OP_MEM and op.mem.base == X86_REG_RIP:
                    refs.append(insn.address + insn.size + op.mem.disp)
                elif op.type == X86_OP_IMM and base <= op.imm < base + len(data):
                    refs.append(op.imm - base)
                elif op.type == X86_OP_MEM and not op.mem.base and base <= op.mem.disp < base + len(data):
                    refs.append(op.mem.disp - base)
                elif op.type == X86_OP_IMM and x64:
                    refs.append(op.imm)
            if args.range or targets.intersection(refs):
                print(f"{insn.address:08x} {insn.mnemonic:8} {insn.op_str}",
                      " ; " + ", ".join(hex(r) for r in refs) if refs else "")


if __name__ == "__main__":
    main()

"""Read-only x86/x64 audio RE; never loads a target module.

Run with local/venv/Scripts/python.exe -I tools/re/audio_static.py MODULE.
All addresses are RVAs (x86 absolute operands retain their preferred VAs).
Use --strings, --guid UUID, --xref RVA, --range START END, --exports.
--verify-note ROOT checks the instruction quotes and hashes in audio-eax.md.
Keep exploratory output in ignored local/.
"""
import argparse
import hashlib
import re
import struct
import uuid
from pathlib import Path

import capstone as cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_REG_RIP
import pefile


class Image:
    def __init__(self, path):
        self.path = Path(path)
        self.raw = self.path.read_bytes()
        self.pe = pefile.PE(data=self.raw)
        machine = self.pe.FILE_HEADER.Machine
        if machine not in (0x14c, 0x8664):
            raise ValueError(f"Unsupported PE machine: {machine:#x}")
        self.base = self.pe.OPTIONAL_HEADER.ImageBase
        self.data = self.pe.get_memory_mapped_image()
        self.md = cs.Cs(cs.CS_ARCH_X86, cs.CS_MODE_64 if machine == 0x8664 else cs.CS_MODE_32)
        self.md.detail = True
        self.md.skipdata = True

    def refs(self, insn):
        if not insn.id:
            return []
        refs = []
        for op in insn.operands:
            if op.type == X86_OP_IMM:
                refs.append(op.imm - self.base if self.base <= op.imm < self.base + len(self.data) else op.imm)
            elif op.type == X86_OP_MEM:
                if op.mem.base == X86_REG_RIP:
                    refs.append(insn.address + insn.size + op.mem.disp)
                elif not op.mem.base and not op.mem.index:
                    refs.append(op.mem.disp - self.base)
        return refs

    def instructions(self):
        for section in self.pe.sections:
            if section.Characteristics & 0x20000000:
                start = section.VirtualAddress
                size = min(section.Misc_VirtualSize, section.SizeOfRawData)
                yield from self.md.disasm(self.data[start:start + size], start)

    @staticmethod
    def line(insn):
        return f"{insn.address:08X} {insn.mnemonic} {insn.op_str}"


def verify_note(root):
    note = Path(__file__).resolve().parents[2] / "research/re/audio-eax.md"
    contents = note.read_text(encoding="utf-8")
    images = {}
    for path, sha in re.findall(r"\| `((?:System|Content)/[^`]+)` \| `([a-f0-9]{64})` \|", contents):
        im = Image(root / path)
        if hashlib.sha256(im.raw).hexdigest() != sha:
            raise ValueError(f"Hash mismatch: {path}")
        images[path] = im
    manifest = note.parents[1] / "fingerprints/efbb-eu-dvd-1.1.sha256"
    known = dict((p.replace("\\", "/"), sha) for sha, p in
                 re.findall(r"([a-f0-9]{64}) \*(.*)", manifest.read_text(encoding="utf-8")))
    for path, im in images.items():
        if hashlib.sha256(im.raw).hexdigest() != known.get(path):
            raise ValueError(f"EU 1.1 manifest mismatch: {path}")
    guid_checks = 0
    for module, im in images.items():
        if not module.endswith("MSystem.dll"):
            continue
        for guid in ("0306a6a8-b224-11d2-99e5-0000e8d8c722",
                     "0306a6a7-b224-11d2-99e5-0000e8d8c722",
                     "a8fa6882-b476-11d3-bdb9-00c0f02ddf87",
                     "a8fa6881-b476-11d3-bdb9-00c0f02ddf87"):
            if uuid.UUID(guid).bytes_le in im.data:
                raise ValueError(f"Unexpected EAX 2/3 GUID in {module}: {guid}")
            guid_checks += 1
    for module, rva, guid in re.findall(
            r"\| `((?:System|Content)/[^`]+)` \| `(0x[a-f0-9]+)` \| `([a-f0-9-]{36})` \|", contents):
        im = images[module]
        start = int(rva, 16)
        if im.data[start:start + 16] != uuid.UUID(guid).bytes_le:
            raise ValueError(f"GUID mismatch: {module}+{rva}")
        guid_checks += 1
    count = 0
    for block in re.findall(r"```asm\n(.*?)```", contents, re.S):
        im = None
        for line in block.splitlines():
            if line.startswith(("; System/", "; Content/")):
                im = images[line[2:]]
            match = re.match(r"([0-9A-Fa-f]+)\s+([a-z]+)\s*(.*)", line)
            if not match:
                continue
            if im is None:
                raise ValueError(f"Missing module annotation: {line}")
            rva = int(match[1], 16)
            insn = next(im.md.disasm(im.data[rva:rva + 15], rva))
            expected = match[2] + match[3].split(";")[0]
            normalize = lambda text: re.sub(r"\s+", "", text).lower()
            if normalize(expected) != normalize(insn.mnemonic + insn.op_str):
                raise ValueError(f"Quote mismatch: {line}; actual {im.line(insn)}")
            count += 1
    if len(images) != 9 or count < 60 or guid_checks < 22:
        raise ValueError(f"Insufficient evidence: {len(images)} images, {count} instructions")
    print(f"PASS: {len(images)} binary/manifest hashes, {count} instruction quotes, {guid_checks} GUID checks")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("module", nargs="?", type=Path)
    parser.add_argument("--strings", action="store_true")
    parser.add_argument("--exports", action="store_true")
    parser.add_argument("--guid", action="append", default=[])
    parser.add_argument("--xref", type=lambda s: int(s, 0), action="append", default=[])
    parser.add_argument("--range", nargs=2, type=lambda s: int(s, 0), action="append", default=[])
    parser.add_argument("--verify-note", type=Path)
    args = parser.parse_args()
    if args.verify_note:
        verify_note(args.verify_note)
        return
    if not args.module:
        parser.error("module required unless --verify-note is used")
    im = Image(args.module)
    print(f"{im.path} base={im.base:#x} machine={im.pe.FILE_HEADER.Machine:#x} "
          f"sha256={hashlib.sha256(im.raw).hexdigest()}")
    targets = {rva: "requested" for rva in args.xref}
    if args.strings:
        for match in re.finditer(rb"[\x20-\x7e]{4,}\x00", im.data):
            if re.search(rb"eax|dsound|reverb|soundenv|snd_|sfx_|sound.*(hardware|3d)", match[0], re.I):
                targets[match.start()] = match[0][:-1].decode("ascii")
    for guid in args.guid:
        needle = uuid.UUID(guid).bytes_le
        if needle not in im.data:
            print(f"absent GUID: {guid}")
        for match in re.finditer(re.escape(needle), im.data):
            targets[match.start()] = guid
    if args.exports:
        for symbol in getattr(getattr(im.pe, "DIRECTORY_ENTRY_EXPORT", None), "symbols", []):
            name = symbol.name.decode() if symbol.name else f"ordinal:{symbol.ordinal}"
            if re.search("eax|sound|reverb|environment", name, re.I):
                print(f"export {symbol.address:#x}: {name}")
    for rva, label in targets.items():
        print(f"target {rva:#x}: {label}")
        width = 8 if im.pe.FILE_HEADER.Machine == 0x8664 else 4
        needle = struct.pack("<Q" if width == 8 else "<I", im.base + rva)
        for match in re.finditer(re.escape(needle), im.data):
            print(f"pointer {match.start():#x} -> {rva:#x}")
    if targets:
        for insn in im.instructions():
            hits = [f"{r:#x} {targets[r]}" for r in im.refs(insn) if r in targets]
            if hits:
                print(im.line(insn) + " ; " + ", ".join(hits))
    for start, end in args.range:
        for insn in im.md.disasm(im.data[start:end], start):
            print(im.line(insn))


if __name__ == "__main__":
    main()

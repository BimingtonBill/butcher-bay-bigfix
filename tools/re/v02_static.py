"""Read-only EU 1.1 AMD64 input/timing/audio/AA investigation.

Run with local/venv/Scripts/python.exe -I. Never loads target DLLs.
Uses the established display_static.Image decoder; addresses are RVAs.
Full output belongs in ignored local/. See the research note for interpretation.
"""
import argparse
import hashlib
import os
import importlib.util
import re
import struct
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "display_static", Path(__file__).with_name("display_static.py"))
display = importlib.util.module_from_spec(spec)
spec.loader.exec_module(display)

PATTERN = re.compile(
    r"DirectInput|DirectSound|dinput|dsound|GetCursorPos|SetCursorPos|Capture|"
    r"RawInput|QueryPerformance|timeGetTime|GetTickCount|PixelFormat|SwapInterval|"
    r"SwapBuffers|[Ss]ensitiv|[Mm]ouse|[Mm]ultisample|GL_ADVPIXELFORMAT|"
    r"[Ff]ocus|[Aa]ctivat|[Pp]ause|[Tt]ime[Ss]tep|[Ss]imulate|[Pp]hysics|"
    r"[Ss]ound.*[Vv]olume|[Vv]olume.*[Ss]ound|[Tt]ime[Ss]cale")


def verify_note(root):
    """Re-decode the note's short excerpts, rather than trusting scratch output."""
    note = Path(__file__).resolve().parents[2] / "research/re/v0.2-input-timing-audio-aa.md"
    contents = note.read_text(encoding="utf-8")
    images = {Path(p).name: display.Image(root / p) for p in display.MODULES}
    manifest = note.parents[1] / "fingerprints/efbb-eu-dvd-1.1.sha256"
    fingerprints = {}
    for line in manifest.read_text(encoding="utf-8").splitlines():
        match = re.match(r"([0-9a-f]{64}) \*(.*)", line)
        if match:
            fingerprints[match[2].replace("\\", "/").lower()] = match[1]
    for path in display.MODULES:
        actual = hashlib.sha256(images[Path(path).name].raw).hexdigest()
        if fingerprints.get(path.lower()) != actual:
            raise RuntimeError(f"{path}: does not match EU 1.1 manifest")
    hash_count = 0
    for name, expected in re.findall(r"\| ([\w.]+) \| `([0-9a-f]{64})` \|", contents):
        actual = hashlib.sha256(images[name].raw).hexdigest()
        if actual != expected:
            raise RuntimeError(f"{name}: hash mismatch {actual}")
        hash_count += 1
    count = 0
    for block in re.findall(r"```asm\n(.*?)```", contents, re.S):
        module = None
        for line in block.splitlines():
            if line.startswith("; ") and line[2:] in images:
                module = line[2:]
                continue
            match = re.match(r"([0-9A-Fa-f]+)\s+([a-z]+)\s*(.*)", line)
            if not match:
                continue
            if module is None:
                raise RuntimeError(f"Missing module annotation: {line}")
            rva = int(match[1], 16)
            im = images[module]
            insn = next(im.md.disasm(im.data[rva:rva + 15], rva))
            expected = match[2] + match[3].split(";")[0]
            actual = insn.mnemonic + insn.op_str
            normalize = lambda s: re.sub(r"\s+", "", s).lower()
            if normalize(expected) != normalize(actual):
                raise RuntimeError(f"{module}+{rva:x}: expected {expected}, got {actual}")
            count += 1
    if hash_count != 4 or count < 60:
        raise RuntimeError(f"Unexpected evidence coverage: hashes={hash_count}, instructions={count}")
    ms = images["MSystem.dll"]
    gl = images["RndrGL.dll"]
    gw = images["GameWorld_Win64_AMD64.dll"]
    checks = [
        struct.unpack_from("<6I", ms.data, 0x1DE810) == (32, 24, 2, 20, 11, 0),
        struct.unpack_from("<Q", ms.data, 0x1DE828)[0] == ms.base + 0x298800,
        ms.data[0x219F10:0x219F20] == bytes.fromhex("602b1d6fa0d5cf11bfc7444553540000"),
        struct.unpack_from("<Q", ms.data, 0x1D5458 + 0x30)[0] == ms.base + 0x9B9D0,
        struct.unpack_from("<Q", ms.data, 0x1D5458 + 0x48)[0] == ms.base + 0x9BAC0,
        struct.unpack_from("<Q", gw.data, 0x1DEF08 + 0xAE8)[0] == gw.base + 0xC910,
        struct.unpack_from("<Q", gl.data, 0x952D8 + 0x180)[0] == gl.base + 0x49340,
        struct.unpack_from("<f", images["SbzEngine.exe"].data, 0xA9B14)[0] == 1.0,
        struct.unpack_from("<d", ms.data, 0x1DE6E0)[0] == 1000.0,
        gl.data[0x95628:0x9563B] == b"OPTG\\UPDATEFROMENV\x00",
    ]
    if not all(checks):
        raise RuntimeError(f"Data/vtable evidence failed: {[i for i, ok in enumerate(checks) if not ok]}")
    print(f"Note evidence PASS: {len(images)} manifest hashes, {hash_count} note hashes, "
          f"{count} instructions, {len(checks)} data/vtable checks")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=os.environ.get("RIDDICK_DIR"), required="RIDDICK_DIR" not in os.environ,
                   help="game folder (default: RIDDICK_DIR)")
    p.add_argument("--module", type=Path)
    p.add_argument("--inventory", action="store_true")
    p.add_argument("--strings", action="store_true")
    p.add_argument("--range", nargs=2, type=display.number)
    p.add_argument("--xref", action="append", type=display.number, default=[])
    p.add_argument("--text", action="append", default=[])
    p.add_argument("--verify", action="store_true")
    p.add_argument("--verify-note", action="store_true")
    p.add_argument("--data", nargs=2, type=display.number, metavar=("RVA", "SIZE"))
    p.add_argument("--export", action="append", default=[])
    p.add_argument("--rtti", action="append", default=[])
    p.add_argument("--table-size", type=display.number, default=0x90)
    p.add_argument("--operand", help="Regex on decoded operands; exploratory candidates only")
    args = p.parse_args()
    if args.verify_note:
        verify_note(args.root)
        return
    paths = [args.module] if args.module else [args.root / x for x in display.MODULES]
    for path in paths:
        im = display.Image(path)
        print(f"\n{path.name} sha256={hashlib.sha256(im.raw).hexdigest()}")
        if args.data:
            start, size = args.data
            for r in range(start, start + size, 16):
                chunk = im.data[r:min(r + 16, start + size)]
                print(f"{r:08x} {chunk.hex(' ')}")
            continue
        if args.export:
            for s in getattr(getattr(im.pe, "DIRECTORY_ENTRY_EXPORT", None), "symbols", []):
                name = s.name.decode() if s.name else ""
                if any(x.lower() in name.lower() for x in args.export):
                    print(f"export 0x{s.address:x}: {name}")
            continue
        if args.rtti:
            for match in re.finditer(rb"\.\?AV[^\x00]+\x00", im.data):
                name = match[0][:-1].decode("ascii", errors="replace")
                if not any(t.lower() in name.lower() for t in args.rtti):
                    continue
                descriptor = match.start() - 16
                for hit in re.finditer(re.escape(struct.pack("<I", descriptor)), im.data):
                    col = hit.start() - 12
                    if col < 0 or struct.unpack_from("<I", im.data, col)[0] not in (0, 1):
                        continue
                    if (struct.unpack_from("<I", im.data, col)[0] == 1 and
                            struct.unpack_from("<I", im.data, col + 20)[0] != col):
                        continue
                    for ptr in re.finditer(re.escape(struct.pack("<Q", im.base + col)), im.data):
                        vt = ptr.start() + 8
                        print(f"RTTI {name} COL=0x{col:x} vtable=0x{vt:x}")
                        for offset in range(0, args.table_size, 8):
                            value = struct.unpack_from("<Q", im.data, vt + offset)[0]
                            if not any(sec.Characteristics & 0x20000000 and
                                       sec.VirtualAddress <= value - im.base <
                                       sec.VirtualAddress + sec.Misc_VirtualSize
                                       for sec in im.pe.sections):
                                break
                            print(f"  +0x{offset:x} -> 0x{value - im.base:x}")
            continue
        if args.range:
            im.dump(*args.range)
            continue
        targets = {}
        if args.inventory:
            targets.update((r, s) for r, s in im.imports.items() if PATTERN.search(s))
            for symbol in getattr(getattr(im.pe, "DIRECTORY_ENTRY_EXPORT", None), "symbols", []):
                name = symbol.name.decode() if symbol.name else ""
                if PATTERN.search(name):
                    print(f"export 0x{symbol.address:x}: {name}")
        if args.strings or args.text:
            for match in re.finditer(rb"[\x20-\x7e]{4,}\x00", im.data):
                s = match[0][:-1].decode("ascii")
                if (args.strings and PATTERN.search(s)) or any(t.lower() in s.lower() for t in args.text):
                    targets[match.start()] = s
            for s in args.text:
                for encoding in ("ascii", "utf-16le"):
                    needle = s.encode(encoding)
                    offset = 0
                    while (offset := im.data.find(needle, offset)) >= 0:
                        targets.setdefault(offset, f"{s} ({encoding})")
                        offset += len(needle)
        targets.update((r, "requested") for r in args.xref)
        for r, s in targets.items():
            print(f"target 0x{r:x}: {s}")
        for r in args.xref:
            needle = struct.pack("<Q", im.base + r)
            offset = 0
            while (offset := im.data.find(needle, offset)) >= 0:
                print(f"pointer 0x{offset:x} -> 0x{r:x}")
                offset += 8
        calls = set()
        for insn in im.instructions():
            hits = [targets[r] for r in im.references(insn) if r in targets]
            if args.operand and re.search(args.operand, insn.op_str):
                hits.append("operand candidate")
            if hits:
                bounds = im.function(insn.address)
                print(f"unwind={bounds} {im.line(insn)} => {', '.join(hits)}")
                if insn.mnemonic in ("call", "jmp") and any(r in im.imports for r in im.references(insn)):
                    calls.add(insn.address)
        if args.verify:
            raw = im.raw_import_calls(targets.keys() & im.imports.keys())
            if calls != raw:
                raise RuntimeError(f"IAT mismatch decoded={calls}, raw={raw}")
            print(f"IAT coverage PASS: {len(calls)} sites")


if __name__ == "__main__":
    main()

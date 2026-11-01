"""Read-only EU 1.1 AMD64 HUD/FOV evidence. Run with local/venv Python -I.

Never loads game modules. Full dumps belong in ignored local/.
"""
import argparse
import hashlib
import os
import math
import re
import runpy
import struct
from pathlib import Path


Image = runpy.run_path(str(Path(__file__).with_name("display_static.py")))["Image"]
MODULES = {
    "exe": "System/Win64_AMD64/SbzEngine.exe",
    "system": "System/Win64_AMD64/MSystem.dll",
    "gl": "System/Win64_AMD64/RndrGL.dll",
    "mxr": "System/Win64_AMD64/MXR.dll",
    "classes": "Content/GameClasses_Win64_AMD64.dll",
    "world": "Content/GameWorld_Win64_AMD64.dll",
}
# Data claims in the note, separate from its re-decoded instruction excerpts.
FLOATS = {
    "system": {0x1CF118: 1.0, 0x1D6CB4: 0.5, 0x1D9A40: 2.0},
    "world": {0x1DDFC0: 640.0, 0x1DDFBC: 480.0, 0x1DDFA0: 90.0,
              0x1DDC84: 1.0},
    "classes": {0x2A7C88: 90.0, 0x2CCF94: 110.0},
}
DOUBLES = {"system": {0x1D9A48: 360.0, 0x1D9A50: math.pi,
                       0x1D6580: 0.75}, "exe": {0xABC58: 4 / 3}}
TABLES = {
    "world": {0x1E59C8: {0x38: 0x38AA0, 0x60: 0x36D80, 0xF8: 0x38E90,
                           0x100: 0x39AD0, 0x1A8: 0x35EF0},
              0x1DEF08: {0x658: 0x136EF0, 0x660: 0x136DB0, 0xA40: 0x5B10,
                           0xA50: 0xB890}},
    "mxr": {0x1FFE18: {0x128: 0x84CB0}},
}
STRINGS = {"system": {0x21393C: b"cos\0", 0x216038: b"sqrt\0"}}


def verify(root):
    repo = Path(__file__).resolve().parents[2]
    note = (repo / "research/re/v0.4-hud-fov.md").read_text(encoding="utf-8")
    manifest = (repo / "research/fingerprints/efbb-eu-dvd-1.1.sha256").read_text(
        encoding="utf-8")
    hashes = {p.replace("\\", "/").lower(): h
              for h, p in re.findall(r"^([0-9a-f]{64}) \*(.+)$", manifest, re.M)}
    images = {}
    data_count = 0
    for key, relative in MODULES.items():
        im = Image(root / relative)
        if hashlib.sha256(im.raw).hexdigest() != hashes[relative.lower()]:
            raise ValueError(f"{relative}: does not match EU 1.1 manifest")
        images[Path(relative).name] = im
        note_hash = re.search(r"\| " + re.escape(Path(relative).name) +
                              r" \| `([0-9a-f]{64})` \|", note)
        if note_hash is None or note_hash[1] != hashes[relative.lower()]:
            raise ValueError(f"{relative}: note fingerprint missing or incorrect")
        for fmt, claims in (("<f", FLOATS.get(key, {})),
                            ("<d", DOUBLES.get(key, {}))):
            for rva, expected in claims.items():
                actual, = struct.unpack_from(fmt, im.data, rva)
                if actual != expected:
                    raise ValueError(f"{relative}+{rva:x}: {actual} != {expected}")
                data_count += 1
        for table, slots in TABLES.get(key, {}).items():
            for slot, expected in slots.items():
                actual, = struct.unpack_from("<Q", im.data, table + slot)
                if actual != im.base + expected:
                    raise ValueError(f"{relative}: table {table:x}+{slot:x}")
                data_count += 1
        for rva, expected in STRINGS.get(key, {}).items():
            if im.data[rva:rva + len(expected)] != expected:
                raise ValueError(f"{relative}+{rva:x}: string mismatch")
            data_count += 1
        print(f"{relative}: SHA-256/data PASS")
    count = 0
    normalize = lambda s: re.sub(r"\s+", "", s).lower()
    for block in re.findall(r"```asm\n(.*?)```", note, re.S):
        im = None
        for line in block.splitlines():
            if line.startswith("; ") and line[2:] in images:
                im = images[line[2:]]
            match = re.match(r"([0-9a-fA-F]+)\s+([a-z]+)\s+(.*)", line)
            if not match:
                continue
            if im is None:
                raise ValueError(f"Missing module annotation: {line}")
            rva = int(match[1], 16)
            insn = next(im.md.disasm(im.data[rva:rva + 15], rva))
            expected = match[2] + match[3].split(";")[0]
            if normalize(insn.mnemonic + insn.op_str) != normalize(expected):
                raise ValueError(f"{im.path.name}+{rva:x}: {im.line(insn)} != {line}")
            count += 1
    if count < 100:
        raise ValueError(f"Unexpectedly little evidence: {count} instructions")
    print(f"Note PASS: {len(images)} hashes, {count} instructions, {data_count} data/vtable checks")


def projection_examples():
    # Derived from CRC_Viewport::Update, for symmetric view, square pixels,
    # FOVAspect=4/3 and SetPixelAspect(1,1). This does not execute target code.
    for width, height in ((640, 480), (1920, 1080), (2560, 1440), (3440, 1440)):
        a = width / height
        fov = math.radians(90)
        m00 = (4 / 3) / (a * math.tan(fov / 2))
        m11 = (4 / 3) / math.tan(fov / 2)
        horizontal = math.degrees(2 * math.atan(1 / m00))
        vertical = math.degrees(2 * math.atan(1 / m11))
        safe_width = height * 4 / 3
        print(f"{width}x{height}: predicted world H={horizontal:.6f} V={vertical:.6f}; "
              f"HUD scaleX={(4 / 3) / a:.6f} left={(width - safe_width) / 2:.3f}")


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--root", type=Path, default=os.environ.get("RIDDICK_DIR"), required="RIDDICK_DIR" not in os.environ,
                   help="game folder (default: RIDDICK_DIR)")
    p.add_argument("--verify-note", action="store_true")
    p.add_argument("--dump", nargs=3, metavar=("MODULE", "START_RVA", "END_RVA"))
    p.add_argument("--projection-examples", action="store_true")
    args = p.parse_args()
    if not (args.verify_note or args.dump or args.projection_examples):
        p.error("choose --verify-note, --dump or --projection-examples")
    if args.verify_note:
        verify(args.root)
    if args.dump:
        key, start, end = args.dump
        if key not in MODULES:
            p.error(f"module must be one of {', '.join(MODULES)}")
        Image(args.root / MODULES[key]).dump(int(start, 0), int(end, 0))
    if args.projection_examples:
        projection_examples()


if __name__ == "__main__":
    main()

"""Read-only Eyeshine/shadow investigation; run with python -I.

Never loads game code. Full disassembly belongs in ignored local/.
"""
import argparse
import hashlib
import importlib.util
import re
import struct
from pathlib import Path

spec = importlib.util.spec_from_file_location("display_static", Path(__file__).with_name("display_static.py"))
display = importlib.util.module_from_spec(spec)
spec.loader.exec_module(display)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("module", type=Path)
    p.add_argument("--pattern", default=r"shadow|eyeshine|nightvision|copytex|copy_depth|pbuffer|fp20ss|shader.?mode|checkpoint|loadgame|benchmark|^play$|^map$|^load$|^exec$|^bind$")
    p.add_argument("--xref", action="append", type=display.number, default=[])
    p.add_argument("--at", action="append", type=display.number, default=[])
    p.add_argument("--range", nargs=2, type=display.number)
    a = p.parse_args()
    im = display.Image(a.module)
    print(f"{a.module.name} base={im.base:x} sha256={hashlib.sha256(im.raw).hexdigest()}")
    if a.range:
        im.dump(*a.range)
        return
    if a.at:
        for at in a.at:
            bounds = im.function(at)
            print("unwind", bounds)
            if bounds:
                im.dump(*bounds)
        return
    pattern = re.compile(a.pattern, re.I)
    targets = {r: s for r, s in im.imports.items() if pattern.search(s)}
    for match in re.finditer(rb"[\x20-\x7e]{3,}", im.data):
        value = match[0].decode("ascii")
        if pattern.search(value):
            targets[match.start()] = value
    for symbol in getattr(getattr(im.pe, "DIRECTORY_ENTRY_EXPORT", None), "symbols", []):
        name = symbol.name.decode() if symbol.name else ""
        if pattern.search(name):
            print(f"export {symbol.address:x} {name}")
    if a.xref:
        targets = {r: "requested" for r in a.xref}
    for r, value in targets.items():
        print(f"target {r:x} {value}")
        if a.xref:
            needle = struct.pack("<Q", im.base + r)
            offset = 0
            while (offset := im.data.find(needle, offset)) >= 0:
                print(f"pointer {offset:x} -> {r:x}")
                offset += 8
    for ins in im.instructions():
        hits = [targets[r] for r in im.references(ins) if r in targets]
        if hits:
            print("unwind", im.function(ins.address), im.line(ins), "=>", ", ".join(hits))


if __name__ == "__main__":
    main()

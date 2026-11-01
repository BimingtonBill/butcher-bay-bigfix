"""Read game PEs as data only; inventory Eyeshine fields and viewport imports."""
import argparse
import importlib.util
import struct
from pathlib import Path
from capstone.x86 import X86_OP_MEM

spec = importlib.util.spec_from_file_location("display", Path(__file__).with_name("display_static.py"))
display = importlib.util.module_from_spec(spec)
spec.loader.exec_module(display)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("module", type=Path)
    p.add_argument("--field", type=display.number, action="append", default=[])
    p.add_argument("--target", type=display.number, action="append", default=[])
    p.add_argument("--imports", default="Viewport|Stencil|Raster|WindowPos|Pixel|ColorMask|Blend|Alpha")
    p.add_argument("--data", type=display.number, action="append", default=[], help="Read 32 bytes at an RVA (hex, dwords, floats, ASCII)")
    a = p.parse_args()
    import re
    im = display.Image(a.module)
    print(f"{a.module.name} base={im.base:x}")
    if a.data:
        for r in a.data:
            data=im.data[r:r+32]
            print(f"{r:x}: {data.hex()} dwords={struct.unpack('<8I',data)} floats={struct.unpack('<8f',data)} ascii={data!r}")
        return
    for r, name in im.imports.items():
        if re.search(a.imports, name, re.I):
            print(f"import {r:x} {name}")
    for ins in im.instructions():
        if not ins.id:
            continue
        fields = [op.mem.disp for op in ins.operands if op.type == X86_OP_MEM and op.mem.disp in a.field]
        targets = [r for r in im.references(ins) if r in a.target]
        if fields or targets:
            print("function", im.function(ins.address), im.line(ins))
    for target in a.target:
        needle = struct.pack("<Q", im.base + target)
        offset = 0
        while (offset := im.data.find(needle, offset)) >= 0:
            print(f"pointer {offset:x} -> {target:x}")
            offset += 8


if __name__ == "__main__":
    main()

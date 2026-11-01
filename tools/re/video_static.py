"""Read-only video RE evidence; never loads game code. Run Python with -I.

Uses the same pefile/Capstone environment as display_static.py. The Ogg reader
only reconstructs initial packets (bounded to 64 KiB), not media payloads.
"""
import argparse
import hashlib
import runpy
import struct
from pathlib import Path


Image = runpy.run_path(str(Path(__file__).with_name("display_static.py")))["Image"]
MODULES = {
    "system": "System/Win64_AMD64/MSystem.dll",
    "world": "Content/GameWorld_Win64_AMD64.dll",
    "gl": "System/Win64_AMD64/RndrGL.dll",
}
HASHES = {
    "system": "b59eb2cc98ea40e9a5d8d79f67027965bb4bbb8b93c9e8cea8cb46c81d689500",
    "world": "749d018c8dcded113cc4fb0b1de574e2543e31de61a6b842f2359fe4cde2e5c7",
    "gl": "400dc6b50f65c9b884e85daeac7b1b1aaa2f19d2cbf4dbd9e597abae284ea348",
}
# Selected instruction boundaries, independently re-decoded by --verify.
EVIDENCE = {
    "system": {
        0xF0143: ("call", "0x148740"),
        0xF020D: ("call", "0x148740"),
        0xF0355: ("call", "0x148920"),
        0xF0509: ("mov", "byte ptr [rsi + 0x2f0], 1"),
        0xF0654: ("cmp", "byte ptr [rcx + 0x2f0], 0"),
        0xF068F: ("call", "0x1482a0"),
        0xEF90E: ("call", "0x148a30"),
        0xEFA5D: ("call", "0x148b30"),
        0xF2079: ("call", "0xefff0"),
        0xF20BE: ("mov", "dword ptr [rdi + 0x90], ecx"),
        0xF20DF: ("mov", "dword ptr [rdi + 0x94], ebx"),
        0xF2DA5: ("mov", "eax, dword ptr [rcx + 0x248]"),
        0x14840E: ("mov", "dword ptr [rbx + 8], eax"),
        0x14841E: ("mov", "dword ptr [rbx + 0xc], eax"),
        0x14846E: ("mov", "dword ptr [rbx + 0x20], eax"),
        0x14847E: ("mov", "dword ptr [rbx + 0x24], eax"),
    },
    "world": {
        0xCB754: ("lea", "rdx, [rip + 0x12dcad]"),
        0xCB7CB: ("lea", "rdx, [rip + 0x12dbd6]"),
        0x755D0: ("mov", "rax, rsp"),
        0x75756: ("mov", "dword ptr [rsp + 0xd0], 0x280"),
        0x75761: ("mov", "dword ptr [rsp + 0xd4], 0x1e0"),
        0x757B9: ("call", "qword ptr [rax + 0xf8]"),
        0x75932: ("lea", "rax, [rip + 0x17c577]"),
        0x76407: ("call", "0x3e010"),
        0x7643F: ("movss", "xmm0, dword ptr [rsi + 0x238]"),
        0x7645D: ("call", "qword ptr [rbx + 0x100]"),
        0x39B8F: ("mulss", "xmm6, xmm0"),
        0x39B93: ("divss", "xmm5, xmm6"),
        0x39CD0: ("call", "qword ptr [rax + 0xf8]"),
        0x3D9D4: ("call", "qword ptr [rax + 0x98]"),
    },
    "gl": {
        0x9658: ("call", "qword ptr [rip + 0x8557a]"),
        0x9692: ("call", "qword ptr [rip + 0x855c0]"),
        0x3ACDE: ("call", "qword ptr [rip + 0x53f8c]"),
        0x3AE85: ("call", "qword ptr [rip + 0x53de5]"),
        0x3AEE3: ("call", "qword ptr [rip + 0x53d87]"),
        0x4812A: ("call", "qword ptr [rip + 0x46a60]"),
    },
}


def identification(path):
    with path.open("rb") as stream:
        data = stream.read(65536)
    packets = {}
    offset = 0
    while offset + 27 <= len(data):
        header = data[offset:offset + 27]
        if header[:5] != b"OggS\0":
            raise ValueError(f"{path.name}: invalid Ogg page at {offset}")
        serial = struct.unpack_from("<I", header, 14)[0]
        count = header[26]
        lacing = data[offset + 27:offset + 27 + count]
        if len(lacing) != count:
            break
        position = offset + 27 + count
        if position + sum(lacing) > len(data):
            break
        packet = packets.setdefault(serial, bytearray())
        for size in lacing:
            packet.extend(data[position:position + size])
            position += size
            if size < 255:
                if packet[:7] == b"\x80theora":
                    if len(packet) < 42:
                        raise ValueError(f"{path.name}: truncated Theora identification")
                    be = lambda a, b: int.from_bytes(packet[a:b], "big")
                    return (tuple(packet[7:10]), be(10, 12) * 16, be(12, 14) * 16,
                            be(14, 17), be(17, 20), packet[20], packet[21],
                            be(30, 33), be(33, 36))
                packet.clear()
        offset = position
    raise ValueError(f"{path.name}: no Theora identification in first 64 KiB")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--verify", action="store_true")
    parser.add_argument("--dump", nargs=3, metavar=("MODULE", "START", "END"),
                        help="module is system/world/gl; addresses are RVAs")
    args = parser.parse_args()
    for path in sorted((args.root / "Content/Videos").glob("*.ogg")):
        version, fw, fh, pw, ph, x, y, pn, pd = identification(path)
        print(f"{path.name}: version={version} frame={fw}x{fh} picture={pw}x{ph} "
              f"offset={x},{y} raw_par={pn}/{pd}")
    if args.verify:
        count = 0
        for key, relative in MODULES.items():
            im = Image(args.root / relative)
            if hashlib.sha256(im.raw).hexdigest() != HASHES[key]:
                raise ValueError(f"Unrecognised {relative}")
            for at, expected in EVIDENCE[key].items():
                instruction = next(im.md.disasm(im.data[at:at + 15], at))
                actual = instruction.mnemonic, instruction.op_str
                if actual != expected:
                    raise ValueError(f"{relative} {at:x}: {actual} != {expected}")
                count += 1
            if key == "world":
                for slot, rva in ((0x100, 0x39AD0), (0xF8, 0x38E90)):
                    target, = struct.unpack_from("<Q", im.data, 0x1E59C8 + slot)
                    if target != im.base + rva:
                        raise ValueError(f"CRC_Util2D slot +{slot:#x} changed")
            print(f"{relative}: SHA-256 and evidence PASS")
        print(f"Verified {count} instructions and CRC_Util2D fitting/submission vtable slots")
    if args.dump:
        key, start, end = args.dump
        im = Image(args.root / MODULES[key])
        im.dump(int(start, 0), int(end, 0))


if __name__ == "__main__":
    main()

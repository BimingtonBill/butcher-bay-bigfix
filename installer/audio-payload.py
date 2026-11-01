"""POSIX optional-component PE preflight. No DLL loading or third-party modules."""
import hashlib
import json
from pathlib import Path
import struct
import sys


def validate(path, digest, expected):
    data = path.read_bytes()
    if len(digest) != 64 or hashlib.sha256(data).hexdigest() != digest.lower():
        raise ValueError(f"Audio SHA-256 mismatch: {path}")

    def u16(at):
        return struct.unpack_from("<H", data, at)[0]

    def u32(at):
        return struct.unpack_from("<I", data, at)[0]

    pe = u32(0x3c)
    if data[:2] != b"MZ" or data[pe:pe+4] != b"PE\0\0" or u16(pe+4) != 0x8664 or u16(pe+24) != 0x20b:
        raise ValueError(f"Audio AMD64 PE32+ required: {path}")
    table = pe+24+u16(pe+20)

    def rva(value):
        for i in range(u16(pe+6)):
            section = table+40*i
            start, size, offset = u32(section+12), u32(section+16), u32(section+20)
            if start <= value < start+size and offset+value-start < len(data):
                return offset+value-start
        raise ValueError("unmapped audio PE RVA")

    directory = rva(u32(pe+24+112))
    count, names = u32(directory+24), rva(u32(directory+32))
    if count > 65536:
        raise ValueError("invalid audio export count")
    exports = set()
    for i in range(count):
        offset = rva(u32(names+4*i))
        end = data.index(b"\0", offset, min(offset+1024, len(data)))
        exports.add(data[offset:end].decode("ascii"))
    if not set(expected) <= exports:
        raise ValueError(f"Missing audio exports: {path}: {set(expected)-exports}")


if __name__ == "__main__":
    try:
        if sys.argv[1] == "--check-uninstall":
            manifest = json.loads(Path(sys.argv[2]).read_text(encoding="utf-8-sig"))
            for entry in manifest["files"]+[manifest["environmentCfg"]]:
                path = Path(entry["path"])
                if path.is_file() and entry.get("installedSha256") and hashlib.sha256(path.read_bytes()).hexdigest() != entry["installedSha256"]:
                    raise ValueError(f"Uninstall conflict: {path} changed since installation. Move it aside and retry.")
                if entry.get("backup") and not Path(entry["backup"]).is_file():
                    raise ValueError(f"Uninstall conflict: missing backup {entry['backup']}")
            sys.exit(0)
        for line in Path(sys.argv[2]).read_text(encoding="utf-8").splitlines():
            fields = line.split("\t")
            if fields[0] == "audio_file":
                validate(Path(sys.argv[1])/fields[1], fields[2], fields[3].split(","))
    except (ValueError, OSError, IndexError, struct.error) as error:
        sys.exit(f"error: {error}")

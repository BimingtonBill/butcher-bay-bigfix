"""Read-only volunteer fingerprint collector. Python 3.9+, standard library only."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import struct


def file_version(path: Path):
    """Cheap optional Windows metadata query; never load the game as code."""
    if os.name != "nt":
        return None
    import ctypes
    from ctypes import wintypes

    api = ctypes.WinDLL("version.dll")
    api.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    api.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    api.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID]
    api.GetFileVersionInfoW.restype = wintypes.BOOL
    api.VerQueryValueW.argtypes = [wintypes.LPCVOID, wintypes.LPCWSTR,
                                  ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_uint)]
    api.VerQueryValueW.restype = wintypes.BOOL
    size = api.GetFileVersionInfoSizeW(str(path), None)
    if not 52 <= size <= 1024 * 1024:
        return None
    buffer = ctypes.create_string_buffer(size)
    pointer, length = ctypes.c_void_p(), ctypes.c_uint()
    if (not api.GetFileVersionInfoW(str(path), 0, size, buffer) or
            not api.VerQueryValueW(buffer, "\\", ctypes.byref(pointer), ctypes.byref(length)) or
            not pointer.value or length.value < 52):
        return None
    fixed = struct.unpack("<13I", ctypes.string_at(pointer, 52))
    if fixed[0] != 0xfeef04bd:
        return None
    return ".".join(str(n) for n in (fixed[2] >> 16, fixed[2] & 65535, fixed[3] >> 16, fixed[3] & 65535))


def pe_layout(blob: bytes):
    """Validate headers/sections before mapping any RVA to raw file bytes."""
    try:
        if len(blob) < 64 or blob[:2] != b"MZ":
            return None
        nt = struct.unpack_from("<I", blob, 60)[0]
        if nt < 64 or blob[nt:nt+4] != b"PE\0\0":
            return None
        count, stamp = struct.unpack_from("<HI", blob, nt+6)
        optional_size = struct.unpack_from("<H", blob, nt+20)[0]
        optional = nt+24
        section_table = optional+optional_size
        magic = struct.unpack_from("<H", blob, optional)[0]
        headers = struct.unpack_from("<I", blob, optional+60)[0]
        if (magic not in (0x10b, 0x20b) or optional_size < 64 or
                not section_table+count*40 <= headers <= len(blob)):
            return None
        sections = []
        for i in range(count):
            start = section_table+i*40
            _, rva, size, offset = struct.unpack_from("<IIII", blob, start+8)
            sections.append((rva, size, offset))
        return stamp, headers, sections
    except struct.error:
        return None


def file_offset(blob: bytes, layout, rva: int, length: int):
    if layout is None or rva < 0 or length <= 0:
        return None
    _, headers, sections = layout
    if rva < headers:
        return rva if rva+length <= headers else None
    for address, size, offset in sections:
        if address <= rva and rva+length <= address+size:
            if offset >= headers and offset+size <= len(blob):
                return offset+rva-address
    return None


def collect(game: Path, data: dict) -> dict:
    files, images, sites = {}, {}, []
    for name in data["reportFiles"]:
        path = game / name
        if not path.is_file():
            files[name] = {"status": "missing"}
            continue
        try:
            blob = path.read_bytes()
            layout = pe_layout(blob)
            files[name] = {"status": "ok", "sha256": hashlib.sha256(blob).hexdigest(),
                           "size": len(blob), "peTimestamp": layout[0] if layout else None,
                           "fileVersion": file_version(path)}
            images[name] = (blob, layout)
        except OSError:
            files[name] = {"status": "unreadable"}
    for site in data["patchSites"]:
        result = dict(site)
        expected = bytes.fromhex(site["expected"])
        if not 0 < len(expected) <= 96:
            raise ValueError("probe length must be 1..96 bytes")
        blob, layout = images.get(site["file"], (b"", None))
        offset = file_offset(blob, layout, site["rva"], len(expected))
        file_status = files[site["file"]]["status"]
        result.update(fileOffset=offset, actual=None, status="unmapped" if file_status == "ok" else file_status)
        if offset is not None:
            actual = blob[offset:offset+len(expected)]
            result.update(actual=actual.hex(), status="match" if actual == expected else "mismatch")
        # Matching probes are evidence only; code semantics and all hard-coded
        # callers/data references still need review before admitting a new build.
        result["reviewRequired"] = True
        sites.append(result)
    hints = set()
    for child in game.iterdir():
        if child.is_dir() and (child.name == "Content" or child.name.startswith(("Content_", "ExtraContent_"))):
            hints.add(child.name)
            registry = child / "Registry"
            if registry.is_dir():
                hints.update(p.relative_to(game).as_posix() for p in registry.glob("StringTable_*.txt") if p.is_file())
        elif child.is_file() and child.suffix.lower() == ".txt" and child.stem.lower().startswith(("readme", "license")):
            hints.add(child.name)
    return {"schemaVersion": 1, "files": files, "languageHints": sorted(hints), "patchSites": sites,
            "verificationNote": "Reference EU11 RVAs, not approval to patch. Review each site, semantic callers, "
            "internal data offsets and fingerprint gates in the listed plugin sources for a new build. "
            "Export/IAT hooks resolve symbols at runtime and have no fixed byte probe. "
            "fileVersion is optional Windows resource metadata, not identity; peTimestamp is null for invalid PE."}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", required=True, type=Path)
    parser.add_argument("--output", type=Path, default=Path("bigfix-fingerprint.json"))
    default_data = Path(__file__).resolve().parent.parent / "installer/bigfix-data.json"
    if not default_data.exists():
        default_data = Path(__file__).with_name("bigfix-data.json")
    parser.add_argument("--data-file", type=Path, default=default_data)
    args = parser.parse_args()
    game, output = args.game_dir.resolve(), args.output.resolve()
    if game == output or game in output.parents:
        parser.error("output must be outside the game folder (read-only collection)")
    if not game.is_dir():
        parser.error("game folder does not exist")
    try:
        data = json.loads(args.data_file.read_text(encoding="utf-8-sig"))
        for name in data["reportFiles"]:
            resolved = (game / name).resolve()
            if resolved == game or game not in resolved.parents:
                raise ValueError("report path escapes game folder")
        report = collect(game, data)
        # Exclusive create protects existing reports as well as arbitrary files.
        with output.open("x", encoding="utf-8", newline="\n") as handle:
            json.dump(report, handle, indent=2)
            handle.write("\n")
    except (OSError, ValueError, KeyError) as exc:
        parser.error(str(exc))
    print(f"Wrote {output}. Attach it using .github/ISSUE_TEMPLATE/new-build.yml; no game files.")


if __name__ == "__main__":
    main()

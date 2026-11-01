"""Generate installer/bigfix-data.json and bigfix-data.txt from research/fingerprints/*.sha256.

Lists x64 System files, patched Content DLLs and the x86 executables for
recognition only. The plugin remains x64 only.

The JSON is what install.ps1 and install.sh (with python3) read. The flat text
file is the same data for machines without python3: one line per file,
`<build id><TAB><relative path><TAB><sha256>`, plus `install<TAB><file>`,
`required<TAB><patched path>` and `supported<TAB><verified build id>`.

Run from anywhere:  python installer/gen_data.py
The output is deterministic, so re-running it produces identical files.
Import untrusted volunteer hashes as a disabled candidate:
  python installer/gen_data.py --import-report report.json --build-id US11 --label "US 5-CD 1.1"
"""

from __future__ import annotations

import json
import argparse
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FINGERPRINT_DIR = ROOT / "research" / "fingerprints"
OUTPUT = Path(__file__).resolve().parent / "bigfix-data.json"
OUTPUT_TXT = Path(__file__).resolve().parent / "bigfix-data.txt"

# Order matters for humans reading the file; EU11 is the supported build.
BUILD_SOURCES = (
    ("EU11", "efbb-eu-dvd-1.1.sha256", "EU 1.1"),
    ("EU10", "efbb-eu-dvd-1.0.sha256", "EU 1.0"),
)
PLATFORM_DIR = "System/Win64_AMD64/"
INSTALL_FILES = ("BigFix.asi", "BigFix.ini", "winmm.dll")
VOLUNTEER_BUILDS = Path(__file__).with_name("volunteer-builds.json")


def patch_sites() -> list[dict]:
    """Export the plugin's existing verification tables, never infer new offsets.

    Deliberately narrow C++ initializer reader: changes to table syntax must be
    reviewed here too. Generated-data tests pin coverage of the current tables.
    """
    sites = []

    def add(name, module, rva, values, source):
        expected = bytes(int(x.strip(), 0) for x in values.split(",") if x.strip()).hex()
        sites.append(dict(name=name, file=("Content/" if module.startswith("Game") else PLATFORM_DIR) + module,
                          rva=int(rva, 0), expected=expected, source="src/" + source, build="EU11"))

    for source in ("patch_table.cpp", "raw_mouse.cpp", "widescreen_sites.cpp"):
        text = (ROOT / "src" / source).read_text(encoding="utf-8")
        for name, module, rva, values in re.findall(
                r'\{"([!\w]+)",\s*L"([\w.]+)",\s*(0x[\da-fA-F]+),\s*\{([^{}]+)\}', text):
            if name != "skip_startup_logos":
                add(name, module, rva, values, source)
    # This patch uses a C string initializer, including the terminating NUL.
    text = (ROOT / "src/patch_table.cpp").read_text()
    before = re.search(r'const char before\[\] = ("(?:\\.|[^"\\])*");', text)
    logo = re.search(r'PatchSite value\{"(\w+)", L"([\w.]+)", (0x[\da-fA-F]+),', text)
    if not before or not logo:
        raise ValueError("cannot export startup-logo site")
    name, module, rva = logo.groups()
    add(name, module, rva, ",".join(str(x) for x in (json.loads(before[1]) + "\0").encode()), "patch_table.cpp")
    text = (ROOT / "src/video_trace.cpp").read_text()
    for name, module, rva, values in re.findall(
            r'\{"(\w+)", VideoModule::(\w+), (0x[\da-fA-F]+),\s*\{([^{}]+)\}', text):
        add(name, "MSystem.dll" if module == "System" else "GameWorld_Win64_AMD64.dll",
            rva, values, "video_trace.cpp")
    text = (ROOT / "src/eyeshine_variants.h").read_text()
    rva = re.search(r'eyeshineScaleRva=(0x[\da-fA-F]+);', text)
    values = re.search(r'eyeshineScaleBytes\{([^{}]+)\}', text)
    if not rva or not values:
        raise ValueError("cannot export Eyeshine scale site")
    add("eyeshine_scale", "GameWorld_Win64_AMD64.dll", rva[1], values[1], "eyeshine_variants.h")
    # Windowed and borderless policies share the same verified input bytes.
    windowed = next(s for s in sites if s["name"] == "borderless_numeric_selection").copy()
    windowed["name"] = "original_windowed_selection"
    sites.append(windowed)
    return sorted(sites, key=lambda s: (s["file"], s["rva"], s["name"]))


def read_fingerprints(path: Path) -> dict[str, str]:
    """Parse a `sha256sum` listing: `<hex> *<relative/path>`."""
    hashes: dict[str, str] = {}
    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.strip()
        if not line:
            continue
        digest, separator, name = line.partition(" ")
        if not separator or len(digest) != 64:
            raise ValueError(f"{path}: malformed line: {raw_line!r}")
        hashes[name.lstrip("*").strip()] = digest.lower()
    return hashes


def build_data() -> dict:
    sites = patch_sites()
    patched = sorted({site["file"] for site in sites})
    builds = []
    for build_id, filename, label in BUILD_SOURCES:
        hashes = read_fingerprints(FINGERPRINT_DIR / filename)
        files = {
            name: digest
            for name, digest in sorted(hashes.items())
            if name.startswith(PLATFORM_DIR) or name in patched or
            re.fullmatch(r"System/Win32_x86(?:_SSE2?)?/SbzEngine.exe", name)
        }
        if not files:
            raise SystemExit(f"{filename}: no {PLATFORM_DIR} entries found")
        if not set(patched) <= files.keys():
            raise ValueError(f"{filename}: missing patched modules")
        builds.append({"id": build_id, "label": label, "supported": build_id == "EU11", "files": files})
    if VOLUNTEER_BUILDS.exists():
        builds.extend(json.loads(VOLUNTEER_BUILDS.read_text(encoding="utf-8")))
    if len({b["id"] for b in builds}) != len(builds):
        raise ValueError("duplicate build id")
    return {"builds": builds, "patchedFiles": patched,
            "reportFiles": sorted({name for b in builds for name in b["files"]}),
            "patchSites": sites, "install": list(INSTALL_FILES),
            "components": {"dsoal": json.loads(Path(__file__).with_name("dsoal-release.json").read_text(encoding="utf-8"))}}


def import_report(report: dict, build_id: str, label: str, data: dict) -> dict:
    """Reports are untrusted evidence. Import hashes only, disabled by default."""
    if report.get("schemaVersion") != 1:
        raise ValueError("unsupported report schemaVersion")
    if not re.fullmatch(r"[A-Z][A-Z0-9_-]{1,31}", build_id) or build_id in {b["id"] for b in data["builds"]}:
        raise ValueError("invalid or duplicate build id")
    if not label.strip() or len(label) > 160 or any(ord(c) < 32 for c in label):
        raise ValueError("invalid build label")
    files = {}
    for name in data["reportFiles"]:
        entry = report.get("files", {}).get(name, {})
        if entry.get("status") != "ok":
            continue
        digest = entry.get("sha256", "")
        if not isinstance(digest, str) or not re.fullmatch(r"[a-fA-F0-9]{64}", digest):
            raise ValueError(f"invalid sha256: {name}")
        size = entry.get("size")
        if type(size) is not int or size <= 0:
            raise ValueError(f"invalid size: {name}")
        files[name] = digest.lower()
    if not set(data["patchedFiles"]) <= files.keys():
        raise ValueError("report is missing patched files; cannot import a complete build")
    return {"id": build_id, "label": label, "supported": False, "files": files}


def render(data: dict) -> str:
    return json.dumps(data, indent=2) + "\n"


def render_text(data: dict) -> str:
    """The flat file read by install.sh when python3 is not available."""
    lines = [f"required\t{name}" for name in data["patchedFiles"]]
    lines += [f"supported\t{b['id']}" for b in data["builds"] if b["supported"]]
    for build in data["builds"]:
        for name in sorted(build["files"]):
            lines.append(f"{build['id']}\t{name}\t{build['files'][name]}")
    for name in data["install"]:
        lines.append(f"install\t{name}")
    for entry in data.get("components", {}).get("dsoal", {}).get("files", []):
        lines.append("audio_file\t%s\t%s\t%s" % (entry["name"], entry["sha256"], ",".join(entry["exports"])))
    for key, value in data.get("components", {}).get("dsoal", {}).get("environment", {}).items():
        lines.append(f"audio_env\t{key}\t{value}")
    return "\n".join(lines) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--import-report", type=Path)
    parser.add_argument("--build-id")
    parser.add_argument("--label")
    args = parser.parse_args()
    data = build_data()
    if args.import_report:
        if not args.build_id or not args.label:
            parser.error("--import-report requires --build-id and --label")
        try:
            candidate = import_report(json.loads(args.import_report.read_text(encoding="utf-8-sig")),
                                      args.build_id, args.label, data)
        except (ValueError, TypeError, AttributeError) as exc:
            parser.error(str(exc))
        candidates = json.loads(VOLUNTEER_BUILDS.read_text()) if VOLUNTEER_BUILDS.exists() else []
        candidates.append(candidate)
        VOLUNTEER_BUILDS.write_text(render(candidates), encoding="utf-8", newline="\n")
        data = build_data()
        print("Imported pending build; supported=false until maintainer verification.")
    OUTPUT.write_text(render(data), encoding="utf-8", newline="\n")
    OUTPUT_TXT.write_text(render_text(data), encoding="utf-8", newline="\n")
    print(f"wrote {OUTPUT} and {OUTPUT_TXT}")


if __name__ == "__main__":
    main()

"""Tests for tools/package.ps1 and the CI workflow.

Everything runs offline in pytest's tmp_path: the Ultimate ASI Loader archive is
a fake zip built here, and its sha256 is passed through the test-only
-LoaderSha256 parameter. Nothing reads C:\\Games or touches the network.
"""

from __future__ import annotations

import hashlib
import json
import io
import re
import shutil
import subprocess
import zipfile
from pathlib import Path

import pytest

TOOLS_DIR = Path(__file__).resolve().parents[1]
REPO_ROOT = TOOLS_DIR.parent
SCRIPT = TOOLS_DIR / "package.ps1"
VERSION_HEADER = REPO_ROOT / "src" / "version.h"
DEFAULT_INI = REPO_ROOT / "installer" / "BigFix.default.ini"
WORKFLOW = REPO_ROOT / ".github" / "workflows" / "build.yml"

PWSH = shutil.which("pwsh")

# The zip must contain exactly these entries, in this order (fixed package order).
PACKAGE_FILES = [
    "BigFix.asi",
    "BigFix.ini",
    "winmm.dll",
    "install.ps1",
    "BigFixSetup.ps1",
    "BigFixSetup.cmd",
    "install.sh",
    "bigfix-data.json",
    "bigfix-data.txt",
    "README.md",
    "NOTICE.md",
    "LICENSE",
    "docs/VERIFY.md",
    "docs/COMPAT.md",
    "tga2png.py",
    "fingerprint.py",
    "fingerprint.ps1",
    "fingerprint.sh",
    "audio-payload.ps1",
    "audio-payload.py",
    "docs/audio-backend.md",
]

DUMMY_ASI = b"dummy BigFix.asi for the packaging test\n"
DUMMY_LOADER_DLL = b"fake Ultimate ASI Loader dinput8.dll payload\n"


@pytest.fixture(scope="module", autouse=True)
def _require_pwsh():
    if PWSH is None:
        pytest.skip("PowerShell 7 (pwsh) is required to run the packaging tests")


def sha256_file(path: Path) -> str:
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def expected_version() -> str:
    """Mirror package.ps1: kBigFixVersion from src/version.h, sanitised."""
    text = VERSION_HEADER.read_text(encoding="utf-8")
    match = re.search(r'kBigFixVersion\s*=\s*"([^"]+)"', text)
    assert match, "kBigFixVersion not found in src/version.h"
    return re.sub(r"[^0-9A-Za-z.\-]", "-", match.group(1)).strip("-")


@pytest.fixture
def build_dir(tmp_path) -> Path:
    path = tmp_path / "build" / "Release"
    path.mkdir(parents=True)
    (path / "BigFix.asi").write_bytes(DUMMY_ASI)
    return path


@pytest.fixture
def loader_zip(tmp_path) -> Path:
    """A fake Ultimate ASI Loader archive: dinput8.dll sits in a subfolder."""
    path = tmp_path / "loader" / "Ultimate-ASI-Loader-NoPDB_x64.zip"
    path.parent.mkdir()
    with zipfile.ZipFile(path, "w") as archive:
        archive.writestr("Ultimate-ASI-Loader-NoPDB_x64/readme.txt", "fake loader archive\n")
        archive.writestr("Ultimate-ASI-Loader-NoPDB_x64/dinput8.dll", DUMMY_LOADER_DLL)
    return path


def run_package(*args: object, cwd: Path) -> subprocess.CompletedProcess:
    return subprocess.run(
        [PWSH, "-NoProfile", "-NonInteractive", "-File", str(SCRIPT), *[str(a) for a in args]],
        cwd=str(cwd),
        capture_output=True,
        text=True,
    )


def zips_in(directory: Path) -> list[Path]:
    if not directory.exists():
        return []
    return sorted(path for path in directory.iterdir() if path.suffix == ".zip")


def test_package_builds_exact_zip(tmp_path, build_dir, loader_zip):
    out_dir = tmp_path / "dist"
    proc = run_package(
        "-BuildDir", build_dir,
        "-OutDir", out_dir,
        "-LoaderZip", loader_zip,
        "-LoaderSha256", sha256_file(loader_zip),
        cwd=tmp_path,
    )
    assert proc.returncode == 0, proc.stdout + proc.stderr

    version = expected_version()
    zip_path = out_dir / f"BigFix-v{version}.zip"
    assert zips_in(out_dir) == [zip_path]

    with zipfile.ZipFile(zip_path) as archive:
        assert archive.namelist() == PACKAGE_FILES
        assert archive.read("BigFix.asi") == DUMMY_ASI
        # The loader's dinput8.dll is shipped under the name the game loads.
        assert archive.read("winmm.dll") == DUMMY_LOADER_DLL
        # No BigFix.ini in BuildDir, so the default from installer/ is packaged.
        assert archive.read("BigFix.ini") == DEFAULT_INI.read_bytes()
        notices = archive.read("NOTICE.md").decode("utf-8")
        assert "MinHook license" in notices
        assert "Ultimate ASI Loader license" in notices
        assert "Redistributions in binary form" in notices
        for name in ("docs/VERIFY.md", "docs/COMPAT.md"):
            assert archive.read(name) == (REPO_ROOT / name).read_bytes()

    # Same inputs, same bytes: fixed entry order and timestamps.
    second = tmp_path / "dist2"
    proc = run_package(
        "-BuildDir", build_dir,
        "-OutDir", second,
        "-LoaderZip", loader_zip,
        "-LoaderSha256", sha256_file(loader_zip),
        cwd=tmp_path,
    )
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert sha256_file(second / zip_path.name) == sha256_file(zip_path)


def test_package_rejects_wrong_loader_hash(tmp_path, build_dir, loader_zip):
    out_dir = tmp_path / "dist"
    proc = run_package(
        "-BuildDir", build_dir,
        "-OutDir", out_dir,
        "-LoaderZip", loader_zip,
        "-LoaderSha256", "0" * 64,
        cwd=tmp_path,
    )
    assert proc.returncode != 0
    output = (proc.stdout + proc.stderr).lower()
    assert "sha256" in output
    assert zips_in(out_dir) == []


def test_workflow_yaml_parses():
    yaml = pytest.importorskip("yaml")
    document = yaml.safe_load(WORKFLOW.read_text(encoding="utf-8"))
    job = document["jobs"]["windows"]
    assert job["runs-on"] == "windows-latest"
    steps = job["steps"]
    runs = [str(step.get("run", "")) for step in steps]
    assert any("cmake --build" in command for command in runs)
    assert any("ctest --test-dir build -C Release --output-on-failure" in command for command in runs)
    assert any("python -m pytest installer/tests tools/tests -q" in command for command in runs)
    assert any("package.ps1" in command for command in runs)
    uploads = [step for step in steps if "upload-artifact" in str(step.get("uses", ""))]
    assert uploads and uploads[0]["with"]["path"] == "dist/*.zip"


@pytest.mark.parametrize("corrupt", [False, True])
def test_optional_dsoal_offline(tmp_path, build_dir, loader_zip, corrupt):
    # Synthetic PE data only: never execute a DLL and never access the network.
    # Avoid importing pytest's installer conftest into this test collection.
    import struct
    def pe(exports):
        body = bytearray(4096)
        body[:2] = b"MZ"
        struct.pack_into("<I", body, 0x3c, 0x80)
        body[0x80:0x84] = b"PE\0\0"
        struct.pack_into("<HH", body, 0x84, 0x8664, 1)
        struct.pack_into("<H", body, 0x94, 240)
        struct.pack_into("<H", body, 0x98, 0x20b)
        struct.pack_into("<II", body, 0x98+112, 0x1000, 0x200)
        struct.pack_into("<IIII", body, 0x98+240+8, 0x800, 0x1000, 0x800, 0x200)
        struct.pack_into("<I", body, 0x218, len(exports))
        struct.pack_into("<I", body, 0x220, 0x1040)
        for i, name in enumerate(exports):
            offset = 0x300+64*i
            struct.pack_into("<I", body, 0x240+4*i, offset+0xe00)
            body[offset:offset+len(name)+1] = name.encode()+b"\0"
        return bytes(body)
    metadata = json.loads((REPO_ROOT / "installer/dsoal-release.json").read_text())
    inner = io.BytesIO()
    with zipfile.ZipFile(inner, "w") as archive:
        for entry in metadata["files"]:
            body = pe(entry["exports"])
            entry["sha256"] = hashlib.sha256(body).hexdigest()
            archive.writestr("DSOAL/Win64/"+entry["name"], body)
        for name in metadata["notices"]:
            archive.writestr("DSOAL/Documentation/"+name, "fake notice "+name)
    metadata["nestedSha256"] = hashlib.sha256(inner.getvalue()).hexdigest()
    artifact = tmp_path / "audio.zip"
    with zipfile.ZipFile(artifact, "w") as archive:
        archive.writestr(metadata["nestedArchive"], inner.getvalue())
    metadata["sha256"] = sha256_file(artifact) if not corrupt else "0"*64
    for source in metadata["sources"]:
        path = tmp_path / (source["project"]+"-source.zip")
        with zipfile.ZipFile(path, "w") as archive:
            archive.writestr("README", "fake source")
        source["sha256"] = sha256_file(path)
    data = tmp_path / "audio.json"
    data.write_text(json.dumps(metadata))
    out_dir = tmp_path / "dist"
    result = run_package("-BuildDir", build_dir, "-OutDir", out_dir,
                         "-LoaderZip", loader_zip, "-LoaderSha256", sha256_file(loader_zip),
                         "-IncludeDsoal", "-DsoalZip", artifact, "-DsoalDataFile", data,
                         "-DsoalSourceDir", tmp_path, cwd=tmp_path)
    if corrupt:
        assert result.returncode != 0
        assert not zips_in(out_dir)
    else:
        assert result.returncode == 0, result.stdout+result.stderr
        with zipfile.ZipFile(zips_in(out_dir)[0]) as archive:
            assert "dsound.dll" in archive.namelist()
            assert "dsoal-aldrv.dll" in archive.namelist()
            assert "audio/dsoal-source.zip" in archive.namelist()
            assert "audio/Documentation/DSOAL-License.txt" in archive.namelist()
            assert metadata["sources"][0]["revision"] in archive.read("audio/SOURCES.md").decode()

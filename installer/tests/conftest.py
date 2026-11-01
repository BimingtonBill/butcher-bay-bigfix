"""Fixtures for the Windows installer tests.

Everything happens inside pytest's tmp_path: no test touches the real registry,
the real Start menu, or C:\\Games. The installer is driven through
`pwsh -NoProfile -File installer/install.ps1`, exactly as a user would run it.
"""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import subprocess
from pathlib import Path

import pytest

INSTALLER_DIR = Path(__file__).resolve().parents[1]
SCRIPT = INSTALLER_DIR / "install.ps1"
SH_SCRIPT = INSTALLER_DIR / "install.sh"
GEN_DATA = INSTALLER_DIR / "gen_data.py"
DATA_JSON = INSTALLER_DIR / "bigfix-data.json"
DATA_TXT = INSTALLER_DIR / "bigfix-data.txt"
GIT_BASH = Path(r"C:\Program Files\Git\bin\bash.exe")

ENGINE_FILES = {
    "System/Win64_AMD64/SbzEngine.exe": b"dummy SbzEngine.exe for tests\n",
    "System/Win64_AMD64/RndrGL.dll": b"dummy RndrGL.dll for tests\n",
    "System/Win64_AMD64/MSystem.dll": b"dummy MSystem.dll for tests\n",
    "System/Win64_AMD64/dbghelp.dll": b"dummy dbghelp.dll for tests\n",
    "Content/GameWorld_Win64_AMD64.dll": b"dummy GameWorld.dll for tests\n",
    "Content/GameClasses_Win64_AMD64.dll": b"dummy GameClasses.dll for tests\n",
}
INSTALL_FILES = {
    "BigFix.asi": b"dummy BigFix.asi for tests\n",
    "BigFix.ini": b"[BigFix]\nTextureSizeFix=1\n",
    "winmm.dll": b"dummy Ultimate ASI Loader for tests\n",
}
DEFAULT_ENVIRONMENT_CFG = (
    b"; The game rewrites this file.\r\n"
    b"VID_MODE=1280 720 32 60\r\n"
    b"vid_vsync=0\r\n"
    b"XR_SHADERMODE=2\r\n"
    b"SOUND_DEVICE=0\r\n"
    b"SOME_UNKNOWN_KEY=keep me\r\n"
)

STARBREEZE_KEY = (
    r"HKLM\SOFTWARE\WOW6432Node\Starbreeze"
    r"\The Chronicles of Riddick: Escape From Butcher Bay"
)
UNINSTALL_KEY = (
    r"HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall"
    r"\{A8DE8C34-7F51-4cc8-B326-C425793EE741}"
)


def sha256_file(path: Path) -> str:
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def tree_hashes(root: Path) -> dict[str, str]:
    """sha256 of every file under root, keyed by POSIX relative path."""
    root = Path(root)
    return {
        path.relative_to(root).as_posix(): sha256_file(path)
        for path in sorted(root.rglob("*"))
        if path.is_file()
    }


@pytest.fixture(scope="session", autouse=True)
def _require_pwsh():
    if shutil.which("pwsh") is None:
        pytest.skip("PowerShell 7 (pwsh) is required to run the installer tests")


@pytest.fixture
def make_game(tmp_path):
    """Factory: build a fake game tree at an arbitrary path."""

    def _make(path: Path) -> Path:
        path = Path(path)
        for relative, body in ENGINE_FILES.items():
            target = path / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(body)
        (path / "Environment.cfg").write_bytes(DEFAULT_ENVIRONMENT_CFG)
        return path

    return _make


@pytest.fixture
def game_dir(make_game, tmp_path) -> Path:
    return make_game(tmp_path / "sandbox" / "Riddick")


@pytest.fixture
def source_dir(tmp_path) -> Path:
    path = tmp_path / "dist"
    path.mkdir()
    for name, body in INSTALL_FILES.items():
        (path / name).write_bytes(body)
    return path


@pytest.fixture
def shortcut_dir(tmp_path) -> Path:
    path = tmp_path / "Start Menu"
    path.mkdir()
    return path


@pytest.fixture
def make_data(tmp_path):
    """Factory: a fingerprint data file for the dummy files in a game tree.

    match=False simulates a repack: no build in the data file matches.
    """

    def _make(game: Path, build_id: str = "EU11", match: bool = True) -> Path:
        files = {}
        for relative in ENGINE_FILES:
            digest = sha256_file(Path(game) / relative) if match else "0" * 64
            files[relative] = digest
        data = {
            "builds": [{"id": build_id, "label": {"EU11": "EU 1.1", "EU10": "EU 1.0"}.get(build_id, build_id),
                        "files": files, "supported": build_id == "EU11"}],
            "patchedFiles": ["System/Win64_AMD64/RndrGL.dll", "System/Win64_AMD64/MSystem.dll",
                             "Content/GameWorld_Win64_AMD64.dll", "Content/GameClasses_Win64_AMD64.dll"],
            "install": list(INSTALL_FILES),
        }
        suffix = "match" if match else "miss"
        path = tmp_path / f"data-{build_id}-{suffix}.json"
        path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")
        return path

    return _make


@pytest.fixture
def make_registry(tmp_path):
    """Factory: a JSON stand-in for the registry.

    truncate=True reproduces the original installer's bug: every path loses its
    last character (C:\\...\\Riddick -> C:\\...\\Riddic).
    """

    def _make(game: Path, truncate: bool = False) -> Path:
        root = str(Path(game))
        if truncate:
            root = root[:-1]
        registry = {
            STARBREEZE_KEY: {"InstallPath": root + "\\"},
            UNINSTALL_KEY: {
                "InstallLocation": root + "\\",
                "DisplayIcon": root + "\\System\\Win64_AMD64\\SbzEngine.exe",
                "UninstallString": root + "\\Uninstall\\uninstall.exe",
            },
        }
        path = tmp_path / ("registry-truncated.json" if truncate else "registry.json")
        path.write_text(json.dumps(registry, indent=2), encoding="utf-8")
        return path

    return _make


@pytest.fixture
def data_file(make_data, game_dir) -> Path:
    return make_data(game_dir)


@pytest.fixture
def registry_file(make_registry, game_dir) -> Path:
    return make_registry(game_dir)


@pytest.fixture
def run():
    """Invoke the installer through pwsh and capture its output."""

    def _run(*args) -> subprocess.CompletedProcess:
        command = [
            shutil.which("pwsh"),
            "-NoProfile",
            "-File",
            str(SCRIPT),
            *[str(argument) for argument in args],
        ]
        return subprocess.run(
            command,
            cwd=str(INSTALLER_DIR),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=120,
        )

    return _run


# ------------------------------------------------------- Linux installer tests


def find_bash() -> str:
    """The bash that runs install.sh: Git for Windows, never the WSL launcher."""
    candidates = []
    if GIT_BASH.exists():
        candidates.append(GIT_BASH)
    found = shutil.which("bash")
    if found:
        candidates.append(Path(found))
    for candidate in candidates:
        if "system32" in str(candidate).lower():
            continue
        return str(candidate)
    return ""


@pytest.fixture
def bash_exe() -> str:
    bash = find_bash()
    if not bash:
        pytest.skip("bash (Git for Windows) is required to run the Linux installer tests")
    return bash


@pytest.fixture
def run_sh(bash_exe):
    """Invoke install.sh through bash. Paths go in POSIX form, as on Linux."""

    def _run(*args, extra_env=None) -> subprocess.CompletedProcess:
        command = [
            bash_exe,
            SH_SCRIPT.as_posix(),
            *[argument.as_posix() if isinstance(argument, Path) else str(argument) for argument in args],
        ]
        env = dict(os.environ)
        if extra_env:
            env.update(extra_env)
        return subprocess.run(
            command,
            cwd=str(INSTALLER_DIR),
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=120,
            env=env,
        )

    return _run


@pytest.fixture
def make_data_txt(tmp_path):
    """Factory: the flat data file install.sh reads when python3 is missing."""

    def _make(game: Path, build_id: str = "EU11", match: bool = True) -> Path:
        lines = [f"required\t{name}" for name in ENGINE_FILES if name not in (
            "System/Win64_AMD64/SbzEngine.exe", "System/Win64_AMD64/dbghelp.dll")]
        if build_id == "EU11":
            lines.append("supported\tEU11")
        files = {}
        for relative in ENGINE_FILES:
            files[relative] = sha256_file(Path(game) / relative) if match else "0" * 64
        for relative in sorted(files):
            lines.append(f"{build_id}\t{relative}\t{files[relative]}")
        for name in INSTALL_FILES:
            lines.append(f"install\t{name}")
        suffix = "match" if match else "miss"
        path = tmp_path / f"data-{build_id}-{suffix}.txt"
        path.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
        return path

    return _make

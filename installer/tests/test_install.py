"""End-to-end tests for installer/install.ps1.

Each test drives the real script through pwsh against a disposable fake game
tree, a test-only fingerprint file and a JSON stand-in for the registry.
"""

from __future__ import annotations

import importlib.util
import json
import shutil
import subprocess
from pathlib import Path

import pytest

from conftest import (
    DATA_JSON,
    GEN_DATA,
    INSTALL_FILES,
    SCRIPT,
    STARBREEZE_KEY,
    UNINSTALL_KEY,
    tree_hashes,
)

INSTALLED_NAMES = ("BigFix.asi", "BigFix.ini", "winmm.dll")
SHORTCUT_GLOB = "*.lnk"


def installer_args(game, source_dir, data_file, registry_file, shortcut_dir, *extra):
    return [
        "-GameDir", str(game),
        "-SourceDir", str(source_dir),
        "-DataFile", str(data_file),
        "-RegistryFile", str(registry_file),
        "-ShortcutDir", str(shortcut_dir),
        "-DesktopMode", "2560x1440@180",
        *extra,
    ]


def registry_args(source_dir, data_file, registry_file, shortcut_dir, *extra):
    return [
        "-SourceDir", str(source_dir),
        "-DataFile", str(data_file),
        "-RegistryFile", str(registry_file),
        "-ShortcutDir", str(shortcut_dir),
        "-DesktopMode", "2560x1440@180",
        *extra,
    ]


def output_of(result) -> str:
    return (result.stdout or "") + (result.stderr or "")


def read_registry(path: Path) -> dict:
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def assert_registry_repaired(registry_file: Path, game_dir: Path) -> None:
    registry = read_registry(registry_file)
    correct_root = str(game_dir)
    assert registry[STARBREEZE_KEY]["InstallPath"].casefold() == (correct_root + "\\").casefold()
    assert registry[UNINSTALL_KEY]["InstallLocation"].casefold() == (correct_root + "\\").casefold()
    assert registry[UNINSTALL_KEY]["DisplayIcon"].casefold() == (
        correct_root + "\\System\\Win64_AMD64\\SbzEngine.exe"
    ).casefold()
    assert registry[UNINSTALL_KEY]["UninstallString"].casefold() == (
        correct_root + "\\Uninstall\\uninstall.exe"
    ).casefold()


def test_install_copies_and_backs_up(
    game_dir, source_dir, data_file, registry_file, shortcut_dir, run
):
    system = game_dir / "System" / "Win64_AMD64"
    old_loader = b"winmm.dll from another mod\n"
    (system / "winmm.dll").write_bytes(old_loader)

    result = run(*installer_args(game_dir, source_dir, data_file, registry_file, shortcut_dir))
    message = output_of(result)
    assert result.returncode == 0, message

    for name in INSTALLED_NAMES:
        assert (system / name).read_bytes() == (source_dir / name).read_bytes(), name

    backup = system / ".bigfix-backup"
    assert (backup / "winmm.dll").read_bytes() == old_loader
    assert not (backup / "BigFix.asi").exists()
    assert not (backup / "BigFix.ini").exists()

    manifest = json.loads((game_dir / ".bigfix-manifest.json").read_text(encoding="utf-8"))
    entries = {entry["name"]: entry for entry in manifest["files"]}
    assert entries["winmm.dll"]["existed"] is True
    assert entries["winmm.dll"]["backup"]
    assert entries["BigFix.asi"]["existed"] is False
    assert entries["BigFix.asi"]["backup"] is None
    assert manifest["build"] == "EU11"
    assert "EU11" in message

    shortcuts = list(shortcut_dir.glob(SHORTCUT_GLOB))
    assert len(shortcuts) == 1, [str(path) for path in shortcut_dir.iterdir()]


def test_uninstall_restores_byte_identical(
    game_dir, source_dir, data_file, registry_file, shortcut_dir, run
):
    system = game_dir / "System" / "Win64_AMD64"
    (system / "winmm.dll").write_bytes(b"winmm.dll from another mod\n")
    before = tree_hashes(game_dir)
    shortcuts_before = sorted(path.name for path in shortcut_dir.iterdir())

    result = run(*installer_args(game_dir, source_dir, data_file, registry_file, shortcut_dir))
    assert result.returncode == 0, output_of(result)

    result = run("-Uninstall", "-GameDir", game_dir)
    assert result.returncode == 0, output_of(result)

    assert tree_hashes(game_dir) == before
    assert not (game_dir / ".bigfix-manifest.json").exists()
    assert not (system / ".bigfix-backup").exists()
    assert sorted(path.name for path in shortcut_dir.iterdir()) == shortcuts_before


def test_eu10_refused_with_message(
    game_dir, source_dir, shortcut_dir, registry_file, make_data, run
):
    data_file = make_data(game_dir, build_id="EU10")
    before = tree_hashes(game_dir)

    result = run(*installer_args(game_dir, source_dir, data_file, registry_file, shortcut_dir))
    message = output_of(result)
    assert result.returncode == 2, message
    assert "1.1" in message
    assert "patch" in message.lower()
    assert "no files were changed" in message.lower()
    assert tree_hashes(game_dir) == before
    assert not (game_dir / ".bigfix-manifest.json").exists()
    assert not list(shortcut_dir.glob(SHORTCUT_GLOB))


def test_unknown_build_refuses_and_explains_report(
    game_dir, source_dir, shortcut_dir, registry_file, make_data, run
):
    data_file = make_data(game_dir, build_id="EU77", match=False)

    result = run(*installer_args(game_dir, source_dir, data_file, registry_file, shortcut_dir))
    message = output_of(result)
    assert result.returncode == 1, message
    assert "unrecognised" in message.lower()
    assert "fingerprint.ps1" in message
    assert ".github/ISSUE_TEMPLATE/new-build.yml" in message
    assert not (game_dir / ".bigfix-manifest.json").exists()


def test_environment_cfg_merges_and_sets_mode(
    game_dir, source_dir, data_file, registry_file, shortcut_dir, run
):
    cfg = game_dir / "Environment.cfg"
    original = cfg.read_bytes()

    result = run(*installer_args(game_dir, source_dir, data_file, registry_file, shortcut_dir))
    assert result.returncode == 0, output_of(result)

    raw = cfg.read_bytes()
    assert b"\r\n" in raw
    text = raw.decode("utf-8")

    values: dict[str, list[str]] = {}
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped or stripped[0] in ";#":
            continue
        if "=" not in stripped:
            continue
        key, value = stripped.split("=", 1)
        values.setdefault(key.strip().upper(), []).append(value)

    assert values["VID_MODE"] == ["2560 1440 32 180"]
    assert values["VID_DWIDTH"] == ["2560"]
    assert values["VID_DHEIGHT"] == ["1440"]
    assert values["XR_SHADERMODE"] == ["5"]
    assert values["VID_VSYNC"] == ["1"]
    assert values["SOUND_DEVICE"] == ["0"]
    assert values["SOME_UNKNOWN_KEY"] == ["keep me"]
    assert "; The game rewrites this file." in text

    backup = game_dir / "System" / "Win64_AMD64" / ".bigfix-backup" / "Environment.cfg"
    assert backup.read_bytes() == original


def test_paths_with_spaces_and_unicode(
    tmp_path, make_game, make_data, make_registry, source_dir, shortcut_dir, run
):
    game = make_game(tmp_path / "My Games" / "Riddick ÿ'")
    data_file = make_data(game)
    registry_file = make_registry(game)
    before = tree_hashes(game)

    result = run(*installer_args(game, source_dir, data_file, registry_file, shortcut_dir))
    message = output_of(result)
    assert result.returncode == 0, message

    system = game / "System" / "Win64_AMD64"
    for name in INSTALLED_NAMES:
        assert (system / name).read_bytes() == (source_dir / name).read_bytes(), name
    assert (game / "Environment.cfg").is_file()

    result = run("-Uninstall", "-GameDir", game)
    assert result.returncode == 0, output_of(result)
    assert tree_hashes(game) == before
    assert not list(shortcut_dir.glob(SHORTCUT_GLOB))


def test_truncated_installpath_repaired(
    game_dir, source_dir, shortcut_dir, make_data, make_registry, run
):
    data_file = make_data(game_dir)
    registry_file = make_registry(game_dir, truncate=True)

    result = run(*registry_args(source_dir, data_file, registry_file, shortcut_dir))
    message = output_of(result)
    assert result.returncode == 0, message
    assert "truncated" in message.lower()
    assert "found the game at" in message.lower()

    assert_registry_repaired(registry_file, game_dir)

    system = game_dir / "System" / "Win64_AMD64"
    for name in INSTALLED_NAMES:
        assert (system / name).read_bytes() == (source_dir / name).read_bytes(), name
    assert (game_dir / ".bigfix-manifest.json").is_file()


def test_truncated_installpath_repaired_with_gamedir(
    game_dir, source_dir, shortcut_dir, make_data, make_registry, run
):
    data_file = make_data(game_dir)
    registry_file = make_registry(game_dir, truncate=True)

    result = run(*installer_args(game_dir, source_dir, data_file, registry_file, shortcut_dir))
    message = output_of(result)
    assert result.returncode == 0, message
    assert "truncated" in message.lower()

    assert_registry_repaired(registry_file, game_dir)

    system = game_dir / "System" / "Win64_AMD64"
    for name in INSTALLED_NAMES:
        assert (system / name).read_bytes() == (source_dir / name).read_bytes(), name
    assert (game_dir / ".bigfix-manifest.json").is_file()


def test_dryrun_changes_nothing(
    game_dir, source_dir, data_file, registry_file, shortcut_dir, run
):
    before = tree_hashes(game_dir)
    registry_before = registry_file.read_bytes()
    shortcuts_before = sorted(path.name for path in shortcut_dir.iterdir())

    result = run(
        *installer_args(
            game_dir, source_dir, data_file, registry_file, shortcut_dir, "-DryRun"
        )
    )
    message = output_of(result)
    assert result.returncode == 0, message
    assert "dry run" in message.lower()

    assert tree_hashes(game_dir) == before
    assert registry_file.read_bytes() == registry_before
    assert sorted(path.name for path in shortcut_dir.iterdir()) == shortcuts_before
    assert not (game_dir / ".bigfix-manifest.json").exists()


def test_data_file_is_up_to_date_and_deterministic():
    spec = importlib.util.spec_from_file_location("bigfix_gen_data", GEN_DATA)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    first = module.render(module.build_data())
    second = module.render(module.build_data())
    assert first == second
    assert DATA_JSON.read_text(encoding="utf-8") == first

    data = json.loads(DATA_JSON.read_text(encoding="utf-8"))
    assert [build["id"] for build in data["builds"]] == ["EU11", "EU10"]
    for build in data["builds"]:
        for relative in (
            "System/Win64_AMD64/SbzEngine.exe",
            "System/Win64_AMD64/RndrGL.dll",
            "System/Win64_AMD64/MSystem.dll",
        ):
            assert relative in build["files"]
    assert data["install"] == list(INSTALL_FILES)


def test_script_parses_under_windows_powershell():
    powershell = shutil.which("powershell")
    if powershell is None:
        pytest.skip("Windows PowerShell 5.1 is not installed")
    escaped = str(SCRIPT).replace("'", "''")
    command = (
        "$errors = $null; $tokens = $null; "
        f"[void][System.Management.Automation.Language.Parser]::ParseFile('{escaped}', "
        "[ref]$tokens, [ref]$errors); "
        "if ($errors.Count -gt 0) { $errors | ForEach-Object { $_.Message }; exit 1 }"
    )
    result = subprocess.run(
        [powershell, "-NoProfile", "-NonInteractive", "-Command", command],
        capture_output=True,
        text=True,
        encoding="utf-8",
        errors="replace",
        timeout=120,
    )
    assert result.returncode == 0, output_of(result)

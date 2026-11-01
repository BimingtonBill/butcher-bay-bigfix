"""End-to-end tests for installer/install.sh, the Linux / Proton / Deck installer.

Each test drives the real script through bash (Git for Windows on this rig; the
script is POSIX sh, so dash and the Deck's bash run the same code) against a
disposable fake game tree and a test-only fingerprint file. The tree layout,
the backup folder and the manifest are the ones install.ps1 uses, so one script
can uninstall what the other installed.
"""

from __future__ import annotations

import importlib.util
import json
import os

import pytest

from conftest import (
    DATA_JSON,
    DATA_TXT,
    GEN_DATA,
    INSTALL_FILES,
    tree_hashes,
)

INSTALLED_NAMES = ("BigFix.asi", "BigFix.ini", "winmm.dll")
STEAM_OPTION = 'WINEDLLOVERRIDES="winmm=n,b" %command%'


def sh_args(game, source_dir, data_file, *extra):
    return [
        "--game-dir", game,
        "--source-dir", source_dir,
        "--data-file", data_file,
        "--desktop-mode", "2560x1440@180",
        *extra,
    ]


def output_of(result) -> str:
    return (result.stdout or "") + (result.stderr or "")


def read_manifest(game):
    return json.loads((game / ".bigfix-manifest.json").read_text(encoding="utf-8"))


def test_sh_install_and_uninstall(
    game_dir, source_dir, make_data_txt, run_sh
):
    data_file = make_data_txt(game_dir)
    system = game_dir / "System" / "Win64_AMD64"
    old_loader = b"winmm.dll from another mod\n"
    (system / "winmm.dll").write_bytes(old_loader)
    before = tree_hashes(game_dir)

    result = run_sh(*sh_args(game_dir, source_dir, data_file))
    message = output_of(result)
    assert result.returncode == 0, message

    for name in INSTALLED_NAMES:
        assert (system / name).read_bytes() == (source_dir / name).read_bytes(), name

    backup = system / ".bigfix-backup"
    assert (backup / "winmm.dll").read_bytes() == old_loader
    assert not (backup / "BigFix.asi").exists()
    assert (backup / "Environment.cfg").is_file()

    manifest = read_manifest(game_dir)
    assert manifest["build"] == "EU11"
    entries = {entry["name"]: entry for entry in manifest["files"]}
    assert entries["winmm.dll"]["existed"] is True
    assert entries["winmm.dll"]["backup"]
    assert entries["BigFix.asi"]["existed"] is False
    assert entries["BigFix.asi"]["backup"] is None
    assert manifest["desktopMode"] == "2560x1440@180"
    assert STEAM_OPTION in message

    result = run_sh("--uninstall", "--game-dir", game_dir)
    assert result.returncode == 0, output_of(result)

    assert tree_hashes(game_dir) == before
    assert not (game_dir / ".bigfix-manifest.json").exists()
    assert not backup.exists()


def test_prints_steam_launch_option(game_dir, run_sh):
    before = tree_hashes(game_dir)

    result = run_sh("--print-steam-options")
    assert result.returncode == 0, output_of(result)
    assert result.stdout.strip() == STEAM_OPTION

    assert tree_hashes(game_dir) == before


def test_sh_explains_non_steam_shortcut_for_deck_path(
    make_game, source_dir, make_data_txt, run_sh, tmp_path
):
    game = make_game(tmp_path / "Steam Library" / "steamapps" / "compatdata" /
                     "123456" / "pfx" / "drive_c" / "Games" / "Riddick")
    data_file = make_data_txt(game)
    result = run_sh(*sh_args(game, source_dir, data_file))
    message = output_of(result)
    assert result.returncode == 0, message
    assert "non-Steam" in message
    assert f'Target: "{game.as_posix()}/System/Win64_AMD64/SbzEngine.exe"' in message
    assert f'Start In: "{game.as_posix()}/System/Win64_AMD64"' in message
    assert "Proton" in message
    assert STEAM_OPTION in message
    assert read_manifest(game)["gameDir"] == game.as_posix()


def test_sh_help_explains_deck_prefix_and_launch_option(run_sh):
    result = run_sh("--help")
    assert result.returncode == 0, output_of(result)
    assert "steamapps/compatdata/<shortcut-id>/pfx" in result.stdout
    assert "non-Steam" in result.stdout
    assert STEAM_OPTION in result.stdout


@pytest.mark.parametrize("dry_run", [False, True])
def test_sh_failed_wine_has_launch_option_and_dry_run_never_calls_wine(
    game_dir, source_dir, make_data_txt, run_sh, tmp_path, dry_run
):
    data_file = make_data_txt(game_dir)
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    fake_wine = bin_dir / "wine"
    fake_wine.write_text(
        '#!/bin/sh\nprintf "%s\\n" "$WINEPREFIX" > "$WINE_CALLED"\nexit 1\n',
        encoding="utf-8", newline="\n",
    )
    fake_wine.chmod(0o755)
    prefix = tmp_path / "Steam Library" / "compatdata" / "123456" / "pfx"
    prefix.mkdir(parents=True)
    called = tmp_path / "wine-called.txt"
    before = tree_hashes(game_dir)
    extra = ["--dry-run"] if dry_run else []
    result = run_sh(
        *sh_args(game_dir, source_dir, data_file), "--prefix", prefix, *extra,
        extra_env={
            "PATH": bin_dir.as_posix() + os.pathsep + os.environ.get("PATH", ""),
            "WINE_CALLED": called.as_posix(),
        },
    )
    message = output_of(result)
    assert result.returncode == 0, message
    assert STEAM_OPTION in message
    if dry_run:
        assert not called.exists()
        assert tree_hashes(game_dir) == before
    else:
        assert called.read_text(encoding="utf-8").strip() == prefix.as_posix()
        assert "wine reg add failed" in message


def test_sh_refuses_eu10(game_dir, source_dir, make_data_txt, run_sh):
    data_file = make_data_txt(game_dir, build_id="EU10")
    before = tree_hashes(game_dir)

    result = run_sh(*sh_args(game_dir, source_dir, data_file))
    message = output_of(result)
    assert result.returncode == 2, message
    assert "1.1" in message
    assert "patch" in message.lower()
    assert "no files were changed" in message.lower()

    assert tree_hashes(game_dir) == before
    assert not (game_dir / ".bigfix-manifest.json").exists()


def test_sh_dry_run_changes_nothing(game_dir, source_dir, make_data_txt, run_sh):
    data_file = make_data_txt(game_dir)
    before = tree_hashes(game_dir)

    result = run_sh(*sh_args(game_dir, source_dir, data_file, "--dry-run"))
    message = output_of(result)
    assert result.returncode == 0, message
    assert "dry run" in message.lower()

    assert tree_hashes(game_dir) == before
    assert not (game_dir / ".bigfix-manifest.json").exists()


def test_sh_unknown_build_refuses(game_dir, source_dir, make_data_txt, run_sh):
    data_file = make_data_txt(game_dir, build_id="EU77", match=False)

    result = run_sh(*sh_args(game_dir, source_dir, data_file))
    message = output_of(result)
    assert result.returncode == 1, message
    assert "unrecognised" in message.lower()
    assert "fingerprint.sh" in message
    assert ".github/ISSUE_TEMPLATE/new-build.yml" in message
    assert not (game_dir / ".bigfix-manifest.json").exists()


def test_sh_environment_cfg_merge(game_dir, source_dir, make_data_txt, run_sh):
    data_file = make_data_txt(game_dir)
    cfg = game_dir / "Environment.cfg"
    original = cfg.read_bytes()

    result = run_sh("--game-dir", game_dir, "--source-dir", source_dir,
                    "--data-file", data_file, "--desktop-mode", "1280x800@60")
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
    assert values["VID_MODE"] == ["1280 800 32 60"]
    assert values["VID_DWIDTH"] == ["1280"]
    assert values["VID_DHEIGHT"] == ["800"]
    assert values["XR_SHADERMODE"] == ["5"]
    assert values["VID_VSYNC"] == ["1"]
    assert values["SOUND_DEVICE"] == ["0"]
    assert values["SOME_UNKNOWN_KEY"] == ["keep me"]
    assert "; The game rewrites this file." in text

    backup = game_dir / "System" / "Win64_AMD64" / ".bigfix-backup" / "Environment.cfg"
    assert backup.read_bytes() == original
    assert read_manifest(game_dir)["desktopMode"] == "1280x800@60"


def test_sh_sets_the_wine_override(
    game_dir, source_dir, make_data_txt, run_sh, tmp_path
):
    """--prefix plus wine on PATH sets winmm=native,builtin in that prefix."""
    data_file = make_data_txt(game_dir)
    bin_dir = tmp_path / "bin"
    bin_dir.mkdir()
    fake_wine = bin_dir / "wine"
    fake_wine.write_text(
        '#!/bin/sh\nprintf "%s\\n" "$@" >> "$WINE_ARGS"\n', encoding="utf-8", newline="\n"
    )
    fake_wine.chmod(0o755)
    prefix = tmp_path / "prefix"
    prefix.mkdir()
    arguments = tmp_path / "wine-args.txt"

    result = run_sh(
        *sh_args(game_dir, source_dir, data_file),
        "--prefix", prefix,
        extra_env={
            "PATH": bin_dir.as_posix() + os.pathsep + os.environ.get("PATH", ""),
            "WINE_ARGS": arguments.as_posix(),
        },
    )
    assert result.returncode == 0, output_of(result)
    assert "set the winmm override" in output_of(result)
    assert arguments.read_text(encoding="utf-8").split("\n")[:7] == [
        "reg", "add", "HKCU\\Software\\Wine\\DllOverrides", "/v", "winmm", "/d", "native,builtin",
    ]


def test_sh_writes_crlf_when_there_is_no_config(
    game_dir, source_dir, make_data_txt, run_sh
):
    """A fresh Environment.cfg is CRLF, exactly as install.ps1 writes it."""
    data_file = make_data_txt(game_dir)
    (game_dir / "Environment.cfg").unlink()

    result = run_sh(*sh_args(game_dir, source_dir, data_file))
    assert result.returncode == 0, output_of(result)

    raw = (game_dir / "Environment.cfg").read_bytes()
    assert raw.endswith(b"\r\n")
    assert b"VID_MODE=2560 1440 32 180\r\n" in raw


def test_sh_invalid_desktop_mode_changes_nothing(
    game_dir, source_dir, make_data_txt, run_sh
):
    data_file = make_data_txt(game_dir)
    before = tree_hashes(game_dir)

    result = run_sh(*sh_args(game_dir, source_dir, data_file, "--desktop-mode", "big"))
    message = output_of(result)
    assert result.returncode == 1, message
    assert "desktop-mode" in message.lower()
    assert tree_hashes(game_dir) == before


def test_sh_reads_the_json_data_file(game_dir, source_dir, make_data, run_sh):
    """With a working python, install.sh reads bigfix-data.json like install.ps1."""
    data_file = make_data(game_dir)

    result = run_sh(*sh_args(game_dir, source_dir, data_file))
    message = output_of(result)
    if result.returncode != 0 and "needs python3" in message:
        pytest.skip("no working python3/python on PATH for this bash")
    assert result.returncode == 0, message
    assert "EU11" in message
    assert read_manifest(game_dir)["build"] == "EU11"


def test_cross_uninstall(
    game_dir, source_dir, data_file, registry_file, shortcut_dir, run, run_sh
):
    """install.ps1 installs, install.sh uninstalls, the tree comes back identical."""
    system = game_dir / "System" / "Win64_AMD64"
    (system / "winmm.dll").write_bytes(b"winmm.dll from another mod\n")
    before = tree_hashes(game_dir)

    result = run(
        "-GameDir", str(game_dir),
        "-SourceDir", str(source_dir),
        "-DataFile", str(data_file),
        "-RegistryFile", str(registry_file),
        "-ShortcutDir", str(shortcut_dir),
        "-DesktopMode", "2560x1440@180",
    )
    assert result.returncode == 0, output_of(result)
    assert (game_dir / ".bigfix-manifest.json").is_file()

    result = run_sh("--uninstall", "--game-dir", game_dir)
    assert result.returncode == 0, output_of(result)

    assert tree_hashes(game_dir) == before
    assert not (game_dir / ".bigfix-manifest.json").exists()
    assert not (system / ".bigfix-backup").exists()


def test_data_files_are_up_to_date_and_deterministic():
    spec = importlib.util.spec_from_file_location("bigfix_gen_data_sh", GEN_DATA)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    first = module.build_data()
    second = module.build_data()
    assert module.render(first) == module.render(second)
    assert module.render_text(first) == module.render_text(second)

    assert DATA_JSON.read_text(encoding="utf-8") == module.render(first)
    assert DATA_TXT.read_text(encoding="utf-8") == module.render_text(first)

    lines = DATA_TXT.read_text(encoding="utf-8").splitlines()
    assert [line for line in lines if line.startswith("install\t")] == [f"install\t{name}" for name in INSTALL_FILES]
    audio = first["components"]["dsoal"]
    assert [line for line in lines if line.startswith("audio_file\t")] == [
        "audio_file\t%s\t%s\t%s" % (e["name"],e["sha256"],",".join(e["exports"])) for e in audio["files"]]
    for line in lines:
        if line.startswith(("required\t", "supported\t", "install\t", "audio_file\t", "audio_env\t")):
            continue
        build, relative, digest = line.split("\t")
        assert build in ("EU11", "EU10")
        assert relative.startswith(("System/", "Content/"))
        assert len(digest) == 64

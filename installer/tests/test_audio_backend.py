"""Optional backend tests use synthetic PE images and injected hashes only."""
import hashlib
import importlib.util
import json
import struct
import os
import ctypes

import pytest
from conftest import tree_hashes
from test_install import installer_args
from test_install_sh import sh_args


def fake_pe(exports, machine=0x8664):
    image = bytearray(4096)
    image[:2] = b"MZ"
    struct.pack_into("<I", image, 0x3c, 0x80)
    image[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HH", image, 0x84, machine, 1)
    struct.pack_into("<H", image, 0x94, 240)
    struct.pack_into("<H", image, 0x98, 0x20b)
    struct.pack_into("<I", image, 0x98+108, 16)
    struct.pack_into("<II", image, 0x98+112, 0x1000, 0x200)
    section = 0x98+240
    struct.pack_into("<IIII", image, section+8, 0x800, 0x1000, 0x800, 0x200)
    struct.pack_into("<I", image, 0x200+24, len(exports))
    struct.pack_into("<I", image, 0x200+32, 0x1040)
    offset = 0x300
    for i, name in enumerate(exports):
        struct.pack_into("<I", image, 0x240+i*4, offset+0xe00)
        image[offset:offset+len(name)+1] = name.encode()+b"\0"
        offset += len(name)+1
    return bytes(image)


@pytest.fixture
def audio_data(data_file, source_dir):
    data = json.loads(data_file.read_text())
    data["components"] = {"dsoal": {"files": [
        {"name": "dsound.dll", "sha256": "", "exports": ["DirectSoundCreate8", "DirectSoundCreate"]},
        {"name": "dsoal-aldrv.dll", "sha256": "", "exports": ["alcGetProcAddress", "alGetProcAddress"]}],
        "environment": {"SND_USE3D": "1", "SND_USEEAX": "1"}}}
    for entry in data["components"]["dsoal"]["files"]:
        body = fake_pe(entry["exports"])
        (source_dir/entry["name"]).write_bytes(body)
        entry["sha256"] = hashlib.sha256(body).hexdigest()
    data_file.write_text(json.dumps(data, indent=2))
    return data_file


@pytest.mark.parametrize("shell", [False, True])
@pytest.mark.parametrize("damage", ["hash", "machine", "export", "missing"])
def test_audio_preflight_no_writes(shell, damage, game_dir, source_dir, audio_data,
                                   registry_file, shortcut_dir, run, run_sh):
    path = source_dir/"dsoal-aldrv.dll"
    data = json.loads(audio_data.read_text())
    if damage == "missing":
        path.unlink()
    elif damage == "hash":
        path.write_bytes(b"wrong")
    else:
        path.write_bytes(fake_pe(["wrong"] if damage == "export" else ["alcGetProcAddress", "alGetProcAddress"],
                                 0x14c if damage == "machine" else 0x8664))
        data["components"]["dsoal"]["files"][1]["sha256"] = hashlib.sha256(path.read_bytes()).hexdigest()
        audio_data.write_text(json.dumps(data))
    before = tree_hashes(game_dir)
    if shell:
        result = run_sh(*sh_args(game_dir, source_dir, audio_data), "--audio-backend", "dsoal")
    else:
        result = run(*installer_args(game_dir, source_dir, audio_data, registry_file, shortcut_dir), "-AudioBackend", "dsoal")
    assert result.returncode != 0
    assert tree_hashes(game_dir) == before


@pytest.mark.parametrize("shell", [False, True])
def test_audio_install_conflict_dry_run_cross_uninstall(shell, game_dir, source_dir, audio_data,
                                                       registry_file, shortcut_dir, run, run_sh):
    system = game_dir/"System/Win64_AMD64"
    (system/"dsound.dll").write_bytes(b"third-party wrapper")
    original = (game_dir/"Environment.cfg").read_bytes()+b"Snd_Use3D=0\r\nSnd_UseEAX=0\r\n"
    (game_dir/"Environment.cfg").write_bytes(original)
    before = tree_hashes(game_dir)
    args = sh_args(game_dir, source_dir, audio_data) if shell else installer_args(game_dir, source_dir, audio_data, registry_file, shortcut_dir)
    runner = run_sh if shell else run
    backend = ["--audio-backend", "dsoal"] if shell else ["-AudioBackend", "DSOAL"]
    result = runner(*args, *backend)
    assert result.returncode != 0
    assert "conflict" in (result.stdout+result.stderr).lower()
    assert tree_hashes(game_dir) == before
    backup = "--backup-audio-conflicts" if shell else "-BackupAudioConflicts"
    result = runner(*args, *backend, backup, "--dry-run" if shell else "-DryRun")
    assert result.returncode == 0, result.stdout+result.stderr
    assert "dsoal-aldrv.dll" in result.stdout and "SND_USEEAX=1" in result.stdout
    assert tree_hashes(game_dir) == before
    result = runner(*args, *backend, backup)
    assert result.returncode == 0, result.stdout+result.stderr
    assert (system/"dsound.dll").read_bytes() == (source_dir/"dsound.dll").read_bytes()
    assert b"SND_USE3D=1" in (game_dir/"Environment.cfg").read_bytes()
    assert b"SND_USEEAX=1" in (game_dir/"Environment.cfg").read_bytes()
    if shell:
        assert 'WINEDLLOVERRIDES="winmm=n,b;dsound=n,b" %command%' in result.stdout
        printed = run_sh("--game-dir", game_dir, "--print-steam-options")
        assert printed.stdout.strip() == 'WINEDLLOVERRIDES="winmm=n,b;dsound=n,b" %command%'
        result = run("-Uninstall", "-GameDir", game_dir)
    else:
        result = run_sh("--uninstall", "--game-dir", game_dir)
    assert result.returncode == 0, result.stdout+result.stderr
    assert tree_hashes(game_dir) == before


@pytest.mark.parametrize("shell", [False, True])
def test_audio_uninstall_preserves_later_changes(shell, game_dir, source_dir, audio_data,
                                                registry_file, shortcut_dir, run, run_sh):
    if shell:
        result = run_sh(*sh_args(game_dir, source_dir, audio_data), "--audio-backend", "dsoal")
    else:
        result = run(*installer_args(game_dir, source_dir, audio_data, registry_file, shortcut_dir), "-AudioBackend", "dsoal")
    assert result.returncode == 0, result.stdout+result.stderr
    cfg = game_dir/"Environment.cfg"
    cfg.write_bytes(cfg.read_bytes().replace(b"SND_USEEAX=1", b"SND_USEEAX=0"))
    before = tree_hashes(game_dir)
    result = run_sh("--uninstall", "--game-dir", game_dir) if shell else run("-Uninstall", "-GameDir", game_dir)
    assert result.returncode != 0
    assert "conflict" in (result.stdout+result.stderr).lower()
    assert tree_hashes(game_dir) == before


def test_audio_ps_failure_rolls_back_files(game_dir, source_dir, audio_data,
                                          registry_file, shortcut_dir, tmp_path, run):
    # Fail after copying DLLs/config, when shortcut creation encounters a file.
    blocked = tmp_path/"blocked-shortcut-folder"
    blocked.write_bytes(b"existing file")
    before = tree_hashes(game_dir)
    result = run(*installer_args(game_dir, source_dir, audio_data, registry_file, blocked), "-AudioBackend", "dsoal")
    assert result.returncode != 0
    assert tree_hashes(game_dir) == before
    assert blocked.read_bytes() == b"existing file"


def test_audio_sh_flat_data(game_dir, source_dir, audio_data, run_sh, tmp_path):
    from conftest import GEN_DATA
    spec = importlib.util.spec_from_file_location("audio_gen", GEN_DATA)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    table = tmp_path/"audio.txt"
    table.write_text(module.render_text(json.loads(audio_data.read_text())), newline="\n")
    before = tree_hashes(game_dir)
    result = run_sh(*sh_args(game_dir, source_dir, table), "--audio-backend", "dsoal")
    assert result.returncode == 0, result.stdout+result.stderr
    result = run_sh("--uninstall", "--game-dir", game_dir)
    assert result.returncode == 0, result.stdout+result.stderr
    assert tree_hashes(game_dir) == before


def test_audio_sh_copy_failure_rolls_back(game_dir, source_dir, audio_data, run_sh, tmp_path):
    scripts = tmp_path/"bin"
    scripts.mkdir()
    cp = scripts/"cp"
    cp.write_text('''#!/bin/sh
for arg do destination=$arg; done
case "$destination" in */System/Win64_AMD64/dsoal-aldrv.dll)
if [ ! -f "$FAIL_ONCE" ]; then
    : >"$FAIL_ONCE"
    exit 1
fi
;; esac
exec /usr/bin/cp "$@"
''', newline="\n")
    cp.chmod(0o755)
    bootstrap = tmp_path/"bootstrap.sh"
    bootstrap.write_text('export PATH="$(cd "$FAIL_BIN" && pwd -P):$PATH"\n', newline="\n")
    before = tree_hashes(game_dir)
    result = run_sh(*sh_args(game_dir, source_dir, audio_data), "--audio-backend", "dsoal",
                    extra_env={"BASH_ENV": bootstrap.as_posix(), "FAIL_BIN": scripts.as_posix(),
                               "FAIL_ONCE": (tmp_path/"failed-once").as_posix()})
    assert result.returncode != 0, result.stdout+result.stderr
    assert "restored the file transaction" in result.stderr
    assert tree_hashes(game_dir) == before


@pytest.mark.skipif(os.name != "nt", reason="Windows sharing-mode rollback")
@pytest.mark.parametrize("shell", [False, True])
def test_audio_locked_destination_rolls_back(shell, game_dir, source_dir, audio_data,
                                            registry_file, shortcut_dir, run, run_sh):
    path = game_dir/"System/Win64_AMD64/dsoal-aldrv.dll"
    path.write_bytes(b"locked third-party renderer")
    before = tree_hashes(game_dir)
    kernel = ctypes.WinDLL("kernel32", use_last_error=True)
    kernel.CreateFileW.restype = ctypes.c_void_p
    kernel.CreateFileW.argtypes = [ctypes.c_wchar_p, ctypes.c_ulong, ctypes.c_ulong,
                                   ctypes.c_void_p, ctypes.c_ulong, ctypes.c_ulong, ctypes.c_void_p]
    kernel.CloseHandle.argtypes = [ctypes.c_void_p]
    handle = kernel.CreateFileW(str(path), 0x80000000, 1, None, 3, 0, None)
    assert handle != ctypes.c_void_p(-1).value
    try:
        if shell:
            result = run_sh(*sh_args(game_dir, source_dir, audio_data), "--audio-backend", "dsoal", "--backup-audio-conflicts")
        else:
            result = run(*installer_args(game_dir, source_dir, audio_data, registry_file, shortcut_dir),
                         "-AudioBackend", "dsoal", "-BackupAudioConflicts")
        assert result.returncode != 0
        assert tree_hashes(game_dir) == before
    finally:
        kernel.CloseHandle(handle)

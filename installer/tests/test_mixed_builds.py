"""Known individual DLL hashes must not authorize an unverified combination."""
import json

import pytest

from conftest import tree_hashes


@pytest.mark.parametrize("shell", ["powershell", "posix-json", "posix-text"])
def test_no_mix_and_match_builds(shell, game_dir, source_dir, make_data, run, run_sh,
                                registry_file, shortcut_dir):
    data_file = make_data(game_dir)
    data = json.loads(data_file.read_text())
    original = data["builds"][0]
    other = {"id": "US11", "supported": True, "files": dict(original["files"])}
    original["files"]["System/Win64_AMD64/MSystem.dll"] = "0" * 64
    other["files"]["System/Win64_AMD64/RndrGL.dll"] = "1" * 64
    data["builds"].append(other)
    if shell == "posix-text":
        data_file = data_file.with_suffix(".txt")
        lines = [f"required\t{name}" for name in data["patchedFiles"]]
        lines += [f"supported\t{b['id']}" for b in data["builds"]]
        lines += [f"{b['id']}\t{name}\t{digest}" for b in data["builds"] for name, digest in b["files"].items()]
        lines += [f"install\t{name}" for name in data["install"]]
        data_file.write_text("\n".join(lines) + "\n")
    else:
        data_file.write_text(json.dumps(data))
    before = tree_hashes(game_dir)
    if shell == "powershell":
        result = run("-GameDir", game_dir, "-SourceDir", source_dir, "-DataFile", data_file,
                     "-RegistryFile", registry_file, "-ShortcutDir", shortcut_dir)
    else:
        result = run_sh("--game-dir", game_dir, "--source-dir", source_dir, "--data-file", data_file)
    message = result.stdout + result.stderr
    assert result.returncode == 1, message
    assert "MSystem.dll" in message and "RndrGL.dll" in message
    assert tree_hashes(game_dir) == before

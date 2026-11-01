"""Both native installers interpret the same generated safety policy."""
import json

import pytest

from conftest import tree_hashes


@pytest.mark.parametrize("shell", ["powershell", "posix-json", "posix-text"])
@pytest.mark.parametrize("change,allowed", [
    ("System/Win64_AMD64/SbzEngine.exe", True),
    ("System/Win64_AMD64/RndrGL.dll", False),
    ("System/Win64_AMD64/MSystem.dll", False),
    ("Content/GameWorld_Win64_AMD64.dll", False),
    ("Content/GameClasses_Win64_AMD64.dll", False),
    ("System/Win64_AMD64/dbghelp.dll", True),
    ("missing-optional", True), ("empty-hash-missing-file", False),
    ("missing", False), ("policy-missing", False), ("unreviewed", False),
])
def test_build_safety(shell, change, allowed, game_dir, source_dir, make_data,
                      make_data_txt, make_registry, shortcut_dir, run, run_sh):
    # A refused build must not repair even a genuinely truncated registry path.
    registry_file = make_registry(game_dir, truncate=not allowed)
    data = make_data(game_dir)
    if shell == "posix-text":
        data = make_data_txt(game_dir)
    if change == "empty-hash-missing-file":
        name = "System/Win64_AMD64/RndrGL.dll"
        (game_dir / name).unlink()
        if shell == "posix-text":
            lines = data.read_text().splitlines()
            lines = ["EU11\t" + name + "\t" if x.startswith("EU11\t" + name + "\t") else x for x in lines]
            data.write_text("\n".join(lines) + "\n")
        else:
            value = json.loads(data.read_text())
            value["builds"][0]["files"][name] = ""
            data.write_text(json.dumps(value))
    elif change == "missing-optional":
        (game_dir / "System/Win64_AMD64/dbghelp.dll").unlink()
    elif change == "missing":
        (game_dir / "System/Win64_AMD64/MSystem.dll").unlink()
    elif change in ("policy-missing", "unreviewed"):
        if shell == "posix-text":
            lines = data.read_text().splitlines()
            prefix = "required\t" if change == "policy-missing" else "supported\t"
            data.write_text("\n".join(x for x in lines if not x.startswith(prefix)) + "\n")
        else:
            value = json.loads(data.read_text())
            if change == "policy-missing":
                value.pop("patchedFiles")
            else:
                value["builds"][0]["supported"] = False
            data.write_text(json.dumps(value))
    else:
        (game_dir / change).write_bytes(b"different repack file")
    before = tree_hashes(game_dir)
    registry_before = registry_file.read_bytes()
    if shell == "powershell":
        result = run("-GameDir", game_dir, "-SourceDir", source_dir, "-DataFile", data,
                     "-RegistryFile", registry_file, "-ShortcutDir", shortcut_dir,
                     "-DesktopMode", "1280x800@60")
    else:
        result = run_sh("--game-dir", game_dir, "--source-dir", source_dir,
                        "--data-file", data, "--desktop-mode", "1280x800@60")
    message = result.stdout + result.stderr
    assert result.returncode == (0 if allowed else 1), message
    if allowed:
        assert "partial match" in message.lower()
        assert (change if change != "missing-optional" else "dbghelp.dll") in message.replace("\\", "/")
    else:
        assert tree_hashes(game_dir) == before
        assert registry_file.read_bytes() == registry_before
        assert not list(shortcut_dir.iterdir())

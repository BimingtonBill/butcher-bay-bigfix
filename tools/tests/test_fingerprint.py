"""Volunteer reports expose only metadata and bounded probes, never game assets."""
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import subprocess
import sys

import pytest

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("volunteer_gen", ROOT / "installer/gen_data.py")
GEN = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(GEN)


def fake_pe():
    data = bytearray(0x400)
    data[:2] = b"MZ"
    struct.pack_into("<I", data, 0x3c, 0x80)
    data[0x80:0x84] = b"PE\0\0"
    struct.pack_into("<HHI", data, 0x84, 0x8664, 1, 123456789)
    struct.pack_into("<H", data, 0x94, 0xf0)
    struct.pack_into("<H", data, 0x98, 0x20b)
    struct.pack_into("<I", data, 0x98 + 60, 0x200)
    data[0x188:0x190] = b".text\0\0\0"
    struct.pack_into("<IIII", data, 0x190, 0x300, 0x1000, 0x200, 0x200)
    data[0x210:0x214] = b"\x48\x83\xec\x28"
    data[0x220:0x224] = b"NOPE"
    return bytes(data)


@pytest.mark.parametrize("wrapper", ["python", "powershell", "posix"])
def test_report_maps_rvas_preserves_game_and_limits_content(tmp_path, wrapper):
    game = tmp_path / "game ÿ' folder"
    engine = game / "System/Win64_AMD64/SbzEngine.exe"
    engine.parent.mkdir(parents=True)
    engine.write_bytes(fake_pe())
    (game / "Content_Rus/Registry").mkdir(parents=True)
    (game / "Content_Rus/Registry/StringTable_Rus.txt").write_text("PRIVATE GAME TEXT")
    missing = "System/Win32_x86/SbzEngine.exe"
    path = "System/Win64_AMD64/SbzEngine.exe"
    sites = [{"name": name, "file": path, "rva": rva, "expected": "4883ec28",
              "source": "src/test.cpp", "build": "EU11"}
             for name, rva in [("match", 0x1010), ("mismatch", 0x1020),
                               ("unbacked", 0x1250), ("cross-end", 0x11ff)]]
    data = tmp_path / "data.json"
    data.write_text(json.dumps({"reportFiles": [path, missing], "patchSites": sites}))
    out = tmp_path / "report.json"
    args = ["--game-dir", str(game), "--output", str(out), "--data-file", str(data)]
    if wrapper == "python":
        command = [sys.executable, str(ROOT / "tools/fingerprint.py"), *args]
    elif wrapper == "powershell":
        if not shutil.which("pwsh"):
            pytest.skip("pwsh unavailable")
        command = [shutil.which("pwsh"), "-NoProfile", "-File", str(ROOT / "tools/fingerprint.ps1"),
                   "-GameDir", str(game), "-Output", str(out), "-DataFile", str(data)]
    else:
        bash = Path("C:/Program Files/Git/bin/bash.exe") if sys.platform == "win32" else shutil.which("sh")
        if not bash or not Path(bash).exists():
            pytest.skip("POSIX shell unavailable")
        command = [str(bash), (ROOT / "tools/fingerprint.sh").as_posix(), *args]
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    report = json.loads(out.read_text())
    assert report["schemaVersion"] == 1
    assert report["files"][path]["sha256"] == hashlib.sha256(fake_pe()).hexdigest()
    assert report["files"][path]["size"] == 0x400
    assert report["files"][path]["peTimestamp"] == 123456789
    assert report["files"][path]["fileVersion"] is None
    assert report["files"][missing]["status"] == "missing"
    assert [s["status"] for s in report["patchSites"]] == ["match", "mismatch", "unmapped", "unmapped"]
    assert report["patchSites"][0]["fileOffset"] == 0x210
    assert report["patchSites"][1]["actual"] == "4e4f5045"
    assert "Content_Rus/Registry/StringTable_Rus.txt" in report["languageHints"]
    assert "PRIVATE GAME TEXT" not in out.read_text()
    assert str(game) not in out.read_text()
    assert engine.read_bytes() == fake_pe()
    assert sorted(p.name for p in game.iterdir()) == ["Content_Rus", "System"]
    # Refuse output inside the game, including overwriting the input executable.
    if wrapper == "python":
        result = subprocess.run([sys.executable, str(ROOT / "tools/fingerprint.py"),
                                 "--game-dir", str(game), "--output", str(engine),
                                 "--data-file", str(data)], capture_output=True, text=True)
        assert result.returncode != 0
        assert engine.read_bytes() == fake_pe()


def test_generated_policy_covers_all_patched_modules_and_x86_engines():
    data = GEN.build_data()
    assert set(data["patchedFiles"]) == {
        "Content/GameClasses_Win64_AMD64.dll", "Content/GameWorld_Win64_AMD64.dll",
        "System/Win64_AMD64/RndrGL.dll", "System/Win64_AMD64/MSystem.dll"}
    assert "System/Win32_x86_SSE2/SbzEngine.exe" in data["reportFiles"]
    assert len(data["patchSites"]) == 27
    assert all(len(bytes.fromhex(s["expected"])) <= 96 for s in data["patchSites"])
    assert {s["file"] for s in data["patchSites"]} == set(data["patchedFiles"])


def test_import_is_pending_and_drops_arbitrary_report_content(tmp_path):
    data = GEN.build_data()
    report = {"schemaVersion": 1, "files": {
        name: {"sha256": "a" * 64, "size": 123, "status": "ok", "payload": "PRIVATE"}
        for name in data["reportFiles"]}, "patchSites": [], "payload": "PRIVATE"}
    candidate = GEN.import_report(report, "US11", "US 5-CD 1.1", data)
    assert candidate["supported"] is False
    assert set(candidate["files"]) == set(data["reportFiles"])
    assert "PRIVATE" not in json.dumps(candidate)
    report["files"][data["patchedFiles"][0]]["sha256"] = "invalid"
    with pytest.raises(ValueError, match="sha256"):
        GEN.import_report(report, "US11", "US 5-CD 1.1", data)


def test_import_command_regenerates_disabled_candidate_and_rejects_duplicate(tmp_path, monkeypatch):
    data = GEN.build_data()
    report = tmp_path / "volunteer.json"
    report.write_text(json.dumps({"schemaVersion": 1, "files": {
        name: {"sha256": "b" * 64, "size": 123, "status": "ok"}
        for name in data["reportFiles"]}}))
    candidates = tmp_path / "volunteer-builds.json"
    output = tmp_path / "bigfix-data.json"
    text = tmp_path / "bigfix-data.txt"
    monkeypatch.setattr(GEN, "VOLUNTEER_BUILDS", candidates)
    monkeypatch.setattr(GEN, "OUTPUT", output)
    monkeypatch.setattr(GEN, "OUTPUT_TXT", text)
    monkeypatch.setattr(sys, "argv", ["gen_data.py", "--import-report", str(report),
                                     "--build-id", "PL11", "--label", "Polish 1.1"])
    GEN.main()
    assert json.loads(candidates.read_text())[0]["supported"] is False
    assert json.loads(output.read_text())["builds"][-1]["id"] == "PL11"
    assert "PL11\tContent/GameWorld_Win64_AMD64.dll\t" in text.read_text()
    assert "supported\tPL11" not in text.read_text()
    before = [p.read_bytes() for p in (candidates, output, text)]
    with pytest.raises(SystemExit):
        GEN.main()
    assert [p.read_bytes() for p in (candidates, output, text)] == before


@pytest.mark.parametrize("mutation", ["schema", "missing", "size", "id", "path"])
def test_import_rejects_invalid_evidence(mutation):
    data = GEN.build_data()
    name = data["patchedFiles"][0]
    report = {"schemaVersion": 1, "files": {
        name: {"sha256": "a" * 64, "size": 123, "status": "ok"}
        for name in data["reportFiles"]}}
    build_id = "RU11"
    if mutation == "schema":
        report["schemaVersion"] = 2
    elif mutation == "missing":
        report["files"].pop(name)
    elif mutation == "size":
        report["files"][name]["size"] = -1
    elif mutation == "id":
        build_id = "EU11"
    else:
        report["files"]["../../SbzEngine.exe"] = report["files"].pop(name)
    with pytest.raises(ValueError):
        GEN.import_report(report, build_id, "Russian 1.1", data)


@pytest.mark.parametrize("blob", [b"", b"MZ", b"MZ" + b"\xff" * 100, b"not a PE"])
def test_malformed_images_are_hashed_but_never_claim_matching_probes(tmp_path, blob):
    path = "System/Win64_AMD64/SbzEngine.exe"
    engine = tmp_path / "game" / path
    engine.parent.mkdir(parents=True)
    engine.write_bytes(blob)
    data = tmp_path / "data.json"
    data.write_text(json.dumps({"reportFiles": [path], "patchSites": [
        {"name": "test", "file": path, "rva": 0x1000, "expected": "4883ec28"}]}))
    output = tmp_path / "report.json"
    command = [sys.executable, str(ROOT / "tools/fingerprint.py"), "--game-dir", str(tmp_path / "game"),
               "--data-file", str(data), "--output", str(output)]
    result = subprocess.run(command, capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    report = json.loads(output.read_text())
    assert report["files"][path]["sha256"] == hashlib.sha256(blob).hexdigest()
    assert report["files"][path]["peTimestamp"] is None
    assert report["patchSites"][0]["status"] == "unmapped"
    before = output.read_bytes()
    assert subprocess.run(command, capture_output=True).returncode != 0
    assert output.read_bytes() == before


@pytest.mark.skipif(sys.platform != "win32", reason="Windows version-resource API")
def test_report_includes_available_windows_version_resource(tmp_path):
    game = tmp_path / "game"
    game.mkdir()
    shutil.copyfile(sys.executable, game / "SbzEngine.exe")
    data = tmp_path / "data.json"
    data.write_text(json.dumps({"reportFiles": ["SbzEngine.exe"], "patchSites": []}))
    output = tmp_path / "report.json"
    result = subprocess.run([sys.executable, str(ROOT / "tools/fingerprint.py"), "--game-dir", str(game),
                             "--data-file", str(data), "--output", str(output)], capture_output=True, text=True)
    assert result.returncode == 0, result.stdout + result.stderr
    version = json.loads(output.read_text())["files"]["SbzEngine.exe"]["fileVersion"]
    assert version.startswith(f"{sys.version_info.major}.{sys.version_info.minor}.")

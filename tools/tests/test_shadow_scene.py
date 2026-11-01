"""Parse and dry-run only: never launch game code or write game files."""
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

SCRIPT = Path(__file__).resolve().parents[1] / "shadow_scene.ps1"
HOSTS = [p for p in [shutil.which("pwsh"),
    str(Path(os.environ.get("SystemRoot", r"C:\Windows")) / "System32/WindowsPowerShell/v1.0/powershell.exe")] if p and Path(p).is_file()]


def quote(text):
    return "'" + str(text).replace("'", "''") + "'"


@pytest.mark.parametrize("host", HOSTS)
def test_parse_and_plan_only_no_game_io(host, tmp_path):
    fake_game = tmp_path / "Nonexistent game"
    output = tmp_path / "No output written"
    code = f"""
$ErrorActionPreference='Stop'
$tokens=$null; $errors=$null
$null=[System.Management.Automation.Language.Parser]::ParseFile({quote(SCRIPT)},[ref]$tokens,[ref]$errors)
if($errors.Count) {{ throw ($errors | Out-String) }}
$plans=@(& {quote(SCRIPT)} -PlanOnly -GameDir {quote(fake_game)} -OutputDir {quote(output)})
$plans | ConvertTo-Json -Compress
"""
    result = subprocess.run([host, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", code], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    plans = json.loads(result.stdout)
    assert [p["Mode"] for p in plans] == [5, 6]
    assert all(p["Scene"] == 'loadgame "Checkpoint"' for p in plans)
    assert all(p["Toggle"] == "cmd togglenightvision" for p in plans)
    assert all('-exec "loadgame \\"Checkpoint\\""' in p["Arguments"] for p in plans)
    assert not fake_game.exists()
    assert not output.exists()


@pytest.mark.parametrize("host", HOSTS)
def test_native_input_helper_compiles_without_sending_input(host):
    code = f"""
$ErrorActionPreference='Stop'
$ast=[System.Management.Automation.Language.Parser]::ParseFile({quote(SCRIPT)},[ref]$null,[ref]$null)
$add=$ast.FindAll({{param($node) $node -is [System.Management.Automation.Language.CommandAst] -and $node.GetCommandName() -eq 'Add-Type'}},$true)
if($add.Count -ne 1) {{ throw 'Expected one native helper definition' }}
& ([scriptblock]::Create($add[0].Extent.Text))
if(-not ('ShadowSceneInput' -as [type])) {{ throw 'No input helper type' }}
'COMPILED'
"""
    result = subprocess.run([host, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", code], capture_output=True, text=True)
    assert result.returncode == 0, result.stderr
    assert "COMPILED" in result.stdout

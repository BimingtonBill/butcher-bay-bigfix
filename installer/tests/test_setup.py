"""The GUI delegates to the real backend; all write tests use disposable trees."""
import json
import os
import shutil
import struct
import subprocess
from pathlib import Path

import pytest

from conftest import INSTALLER_DIR, tree_hashes
from test_audio_backend import audio_data

SETUP = INSTALLER_DIR / "BigFixSetup.ps1"
BACKEND = INSTALLER_DIR / "install.ps1"


def ps(command, host="pwsh"):
    env = dict(os.environ)
    if host == "powershell":
        # A Python child of pwsh inherits its PS7 module path. Windows PowerShell
        # must build its own path, just as it does when launched from our .cmd.
        env = {key: value for key, value in env.items() if key.lower() != "psmodulepath"}
    return subprocess.run(
        [shutil.which(host), "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", command],
        capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=90, env=env,
    )


def literal(value):
    return "'" + str(value).replace("'", "''") + "'"


@pytest.mark.parametrize("host", ["powershell", "pwsh"])
def test_audio_arguments_and_uninstall_mapping(host):
    if not shutil.which(host):
        pytest.skip(f"{host} unavailable")
    result = ps(
        f". {literal(SETUP)}; $s=@{{AudioBackend='dsoal';BackupAudioConflicts=$true;"
        "IniOverrides=@{TraceEax='1'}}; $a=Get-BigFixSetupArguments $s; $s.Uninstall=$true; "
        "@{Install=$a;Uninstall=(Get-BigFixSetupArguments $s)} | ConvertTo-Json -Depth 6", host)
    assert result.returncode == 0, result.stdout + result.stderr
    plans = json.loads(result.stdout)
    assert plans["Install"]["AudioBackend"] == "dsoal"
    assert plans["Install"]["BackupAudioConflicts"] is True
    assert plans["Install"]["IniOverrides"] == {"TraceEax": "1"}
    assert "AudioBackend" not in plans["Uninstall"]
    assert "BackupAudioConflicts" not in plans["Uninstall"]


@pytest.mark.parametrize("installed", [False, True])
def test_inspection_reports_audio_without_writes(
    game_dir, source_dir, data_file, registry_file, shortcut_dir, installed,
):
    system = game_dir / "System/Win64_AMD64"
    (system / "dsound.dll").write_bytes(b"third party")
    if installed:
        (game_dir / ".bigfix-manifest.json").write_text(json.dumps({
            "audioBackend": "dsoal", "files": [{"path": str(system / "dsound.dll")}],
        }))
    before = tree_hashes(game_dir)
    result = ps(backend_command(game_dir, source_dir, data_file, registry_file, shortcut_dir, "-Inspect"))
    assert result.returncode == 0, result.stdout + result.stderr
    info = json.loads(result.stdout)
    assert info["AudioBackend"] == ("dsoal" if installed else "system")
    assert info["AudioPayloadAvailable"] is False
    assert info["AudioConflicts"] == ([] if installed else ["dsound.dll"])
    assert tree_hashes(game_dir) == before


def test_trace_eax_override_preview(game_dir, source_dir, data_file, registry_file, shortcut_dir):
    before = tree_hashes(game_dir)
    result = ps(backend_command(game_dir, source_dir, data_file, registry_file, shortcut_dir,
                               "-WhatIf -IniOverrides @{TraceEax='1'}"))
    assert result.returncode == 0, result.stdout + result.stderr
    assert "TraceEax=1" in result.stdout
    assert tree_hashes(game_dir) == before


def backend_command(game, source, data, registry, shortcuts, extra=""):
    return (
        f"& {literal(BACKEND)} -GameDir {literal(game)} -SourceDir {literal(source)} "
        f"-DataFile {literal(data)} -RegistryFile {literal(registry)} "
        f"-ShortcutDir {literal(shortcuts)} -DesktopMode 2560x1440@180 {extra}"
    )


@pytest.mark.parametrize("host", ["powershell", "pwsh"])
def test_setup_parses_and_headless_plan_uses_backend_defaults(host):
    if not shutil.which(host):
        pytest.skip(f"{host} unavailable")
    result = ps(
        f"$t=$null; $e=$null; [void][System.Management.Automation.Language.Parser]::ParseFile("
        f"{literal(SETUP)},[ref]$t,[ref]$e); if($e.Count){{$e; exit 1}}; "
        f"& {literal(SETUP)} -NoGui -WhatIf -GameDir 'D:\\My Games\\Riddick'", host,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    plan = json.loads(result.stdout)
    assert Path(plan["Script"]) == BACKEND
    assert plan["Arguments"]["GameDir"] == r"D:\My Games\Riddick"
    assert plan["Arguments"]["WhatIf"] is True
    assert plan["Arguments"]["ShaderMode"] == 5
    assert plan["Arguments"]["IniOverrides"]["RawMouse"] == "1"
    assert plan["Arguments"]["IniOverrides"]["FOV"] == "0"


def test_option_state_maps_to_backend_and_uninstall_omits_install_settings():
    result = ps(
        f". {literal(SETUP)}; "
        "$state=@{GameDir='D:\\Riddick'; ShaderMode=6; NoRegistryRepair=$true; "
        "WhatIf=$true; DesktopMode='1920x1080@60'; IniOverrides=@{FOV='90'; AntiAliasing='4'}}; "
        "$a=Get-BigFixSetupArguments $state; $state.Uninstall=$true; "
        "@{Install=$a; Uninstall=(Get-BigFixSetupArguments $state)} | ConvertTo-Json -Depth 6"
    )
    assert result.returncode == 0, result.stdout + result.stderr
    plans = json.loads(result.stdout)
    assert plans["Install"]["ShaderMode"] == 6
    assert plans["Install"]["IniOverrides"] == {"FOV": "90", "AntiAliasing": "4"}
    assert plans["Install"]["NoRegistryRepair"] is True
    assert plans["Install"]["DesktopMode"] == "1920x1080@60"
    assert plans["Uninstall"]["Uninstall"] is True
    assert "IniOverrides" not in plans["Uninstall"]
    assert "ShaderMode" not in plans["Uninstall"]


def test_gui_has_no_backend_mutation_commands_or_duplicate_functions():
    # Catch install logic drifting into the front end, using the parser rather than comments.
    result = ps(
        f"$ast=[System.Management.Automation.Language.Parser]::ParseFile({literal(SETUP)},"
        "[ref]$null,[ref]$null); "
        "$commands=@($ast.FindAll({param($n) $n -is "
        "[System.Management.Automation.Language.CommandAst]},$true) | "
        "ForEach-Object {$_.GetCommandName()}); "
        "$functions=@($ast.FindAll({param($n) $n -is "
        "[System.Management.Automation.Language.FunctionDefinitionAst]},$true) | "
        "ForEach-Object {$_.Name}); @{Commands=$commands;Functions=$functions} | ConvertTo-Json"
    )
    assert result.returncode == 0, result.stdout + result.stderr
    ast = json.loads(result.stdout)
    assert not {"Copy-Item", "Remove-Item", "Set-ItemProperty", "New-StartMenuShortcut"} & set(ast["Commands"])
    assert not {"Invoke-Install", "Invoke-Uninstall", "Find-Build", "Merge-EnvironmentCfg",
                "Repair-TruncatedRegistryPath"} & set(ast["Functions"])
    assert "install.ps1" in SETUP.read_text(encoding="utf-8-sig")


@pytest.mark.parametrize("build,match,status", [("EU11", True, "known"), ("EU10", True, "needs 1.1 patch"), ("OTHER", False, "unknown")])
def test_inspection_is_read_only_and_shares_detection(
    game_dir, source_dir, make_data, make_registry, shortcut_dir, build, match, status,
):
    data = make_data(game_dir, build_id=build, match=match)
    registry = make_registry(game_dir, truncate=True)
    before = tree_hashes(game_dir), registry.read_bytes(), tree_hashes(shortcut_dir)
    result = ps(backend_command(game_dir, source_dir, data, registry, shortcut_dir, "-Inspect"))
    assert result.returncode == 0, result.stdout + result.stderr
    info = json.loads(result.stdout)
    assert info["Status"] == status
    assert info["Build"] == (build if match else None)
    assert info["BuildLabel"] == ({"EU11": "EU 1.1", "EU10": "EU 1.0"}[build] if match else None)
    assert "x" in info["DesktopMode"] and "@" in info["DesktopMode"]
    assert Path(info["GameDir"]) == game_dir
    assert info["NeedsRegistryRepair"] is True
    assert before == (tree_hashes(game_dir), registry.read_bytes(), tree_hashes(shortcut_dir))


@pytest.mark.parametrize("host", ["powershell", "pwsh"])
def test_settings_install_and_uninstall_restore_originals(
    game_dir, source_dir, data_file, registry_file, shortcut_dir, host,
):
    if not shutil.which(host):
        pytest.skip(f"{host} unavailable")
    system = game_dir / "System" / "Win64_AMD64"
    (system / "BigFix.ini").write_text("[BigFix]\nRawMouse=0\n; original\n")
    (source_dir / "BigFix.ini").write_text("; keep comment\n[BigFix]\nRawMouse=1\nCustomKey=keep\n")
    before = tree_hashes(game_dir)
    result = ps(backend_command(game_dir, source_dir, data_file, registry_file, shortcut_dir,
        "-ShaderMode 6 -IniOverrides @{RawMouse='0';FOV='90';HudAspectFix='1';FrameCap='59.94';AntiAliasing='4'}"), host)
    assert result.returncode == 0, result.stdout + result.stderr
    ini = (system / "BigFix.ini").read_text()
    assert "RawMouse=0" in ini
    for setting in ["FOV=90", "HudAspectFix=1", "FrameCap=59.94", "AntiAliasing=4", "CustomKey=keep", "; keep comment"]:
        assert setting in ini
    assert "XR_SHADERMODE=6" in (game_dir / "Environment.cfg").read_text()
    result = ps(backend_command(game_dir, source_dir, data_file, registry_file, shortcut_dir, "-Uninstall"), host)
    assert result.returncode == 0, result.stdout + result.stderr
    assert tree_hashes(game_dir) == before


@pytest.mark.parametrize("setting", ["FOV='59'", "FrameCap='NaN'", "AntiAliasing='3'", "RawMouse='yes'", "Unexpected='1'"])
def test_invalid_settings_fail_before_any_mutation(
    game_dir, source_dir, data_file, make_registry, shortcut_dir, setting,
):
    registry = make_registry(game_dir, truncate=True)
    before = tree_hashes(game_dir), registry.read_bytes()
    result = ps(backend_command(game_dir, source_dir, data_file, registry, shortcut_dir, f"-IniOverrides @{{{setting}}}"))
    assert result.returncode != 0
    assert before == (tree_hashes(game_dir), registry.read_bytes())


def test_selected_settings_whatif_does_not_write(game_dir, source_dir, data_file, registry_file, shortcut_dir):
    before = tree_hashes(game_dir)
    result = ps(backend_command(game_dir, source_dir, data_file, registry_file, shortcut_dir,
        "-WhatIf -ShaderMode 6 -IniOverrides @{FOV='100';AntiAliasing='8'}"))
    assert result.returncode == 0, result.stdout + result.stderr
    assert "XR_SHADERMODE=6" in result.stdout
    assert "FOV=100" in result.stdout
    assert tree_hashes(game_dir) == before


def test_invalid_desktop_mode_fails_before_any_mutation(game_dir, source_dir, data_file, make_registry, shortcut_dir):
    registry = make_registry(game_dir, truncate=True)
    before = tree_hashes(game_dir), registry.read_bytes(), tree_hashes(shortcut_dir)
    result = ps(backend_command(game_dir, source_dir, data_file, registry, shortcut_dir).replace(
        "-DesktopMode 2560x1440@180", "-DesktopMode typo"))
    assert result.returncode != 0
    assert before == (tree_hashes(game_dir), registry.read_bytes(), tree_hashes(shortcut_dir))


@pytest.mark.parametrize("host", ["powershell", "pwsh"])
def test_gui_process_transport_delegates_unicode_and_quoted_paths(
    tmp_path, make_game, source_dir, make_data, make_registry, shortcut_dir, host,
):
    if not shutil.which(host):
        pytest.skip(f"{host} unavailable")
    game = make_game(tmp_path / "Riddick ÿ' & spaces")
    data = make_data(game)
    registry = make_registry(game)
    before = tree_hashes(game)
    args = {
        "GameDir": str(game), "SourceDir": str(source_dir), "DataFile": str(data),
        "RegistryFile": str(registry), "ShortcutDir": str(shortcut_dir),
        "DesktopMode": "2560x1440@180", "WhatIf": True, "ShaderMode": 6,
        "IniOverrides": {"FOV": "90"},
    }
    # This is the exact child-process transport used by the window.
    props = ";".join(f"{key}={literal(value)}" for key, value in args.items() if key not in ("IniOverrides", "WhatIf", "ShaderMode"))
    result = ps(
        f". {literal(SETUP)}; $p=New-BigFixBackendProcess @{{{props};WhatIf=$true;ShaderMode=6;IniOverrides=@{{FOV='90'}}}}; "
        "$o=$p.StandardOutput.ReadToEndAsync(); $e=$p.StandardError.ReadToEndAsync(); $p.WaitForExit(); "
        "$o.GetAwaiter().GetResult(); $e.GetAwaiter().GetResult(); exit $p.ExitCode", host,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert "XR_SHADERMODE=6" in result.stdout
    assert "FOV=90" in result.stdout
    assert tree_hashes(game) == before


@pytest.mark.parametrize("host", ["powershell", "pwsh"])
@pytest.mark.parametrize("dpi", [96, 144])
def test_form_renders_offscreen_without_changing_game(
    tmp_path, game_dir, data_file, registry_file, host, dpi,
):
    if not shutil.which(host):
        pytest.skip(f"{host} unavailable")
    if not shutil.which("powershell"):
        pytest.skip("WinForms rendering requires Windows")
    image = tmp_path / "setup.png"
    before = tree_hashes(game_dir)
    result = ps(
        f". {literal(SETUP)}; Show-BigFixSetup @{{GameDir={literal(game_dir)};"
        f"DataFile={literal(data_file)};RegistryFile={literal(registry_file)}}} {literal(image)} {dpi} Audio", host,
    )
    assert result.returncode == 0, result.stdout + result.stderr
    assert image.read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
    assert image.stat().st_size > 10_000
    width, height = struct.unpack(">II", image.read_bytes()[16:24])
    assert width >= 1000 * dpi / 96
    assert height >= 850 * dpi / 96
    assert tree_hashes(game_dir) == before


@pytest.mark.parametrize("host", ["powershell", "pwsh"])
@pytest.mark.parametrize("build,match,install_allowed,installed", [
    ("EU11", True, True, False), ("EU11", True, True, True),
    ("EU10", True, False, False), ("OTHER", False, False, False),
])
def test_configuration_sections_and_collapsed_log_are_keyboard_accessible(
    game_dir, make_data, registry_file, host, build, match, install_allowed, installed,
):
    if not shutil.which(host) or not shutil.which("powershell"):
        pytest.skip("Windows and the selected PowerShell host are required")
    if installed:
        (game_dir / ".bigfix-manifest.json").write_text("{}")
    before = tree_hashes(game_dir), registry_file.read_bytes()
    data_file = make_data(game_dir, build_id=build, match=match)
    allowed = "$true" if install_allowed else "$false"
    installed_ps = "$true" if installed else "$false"
    command = f". {literal(SETUP)}; $installAllowed={allowed}; $installed={installed_ps}; " + r"""
Add-Type -AssemblyName System.Windows.Forms
$probe=@{Failure=''; Done=$false}; $watch=[Diagnostics.Stopwatch]::StartNew()
$observer=New-Object Windows.Forms.Timer; $observer.Interval=80
$observer.Add_Tick({
    $window=@([Windows.Forms.Application]::OpenForms) | Select-Object -First 1
    if(-not $window){return}
    $window.Opacity=0; $window.Location=New-Object Drawing.Point(-32000,-32000)
    try {
        if($watch.Elapsed.TotalSeconds -gt 20){throw 'Build inspection did not finish'}
        $controls=New-Object 'Collections.Generic.List[Windows.Forms.Control]'
        $pending=New-Object Collections.Queue; $pending.Enqueue($window)
        while($pending.Count){
            $control=$pending.Dequeue(); $controls.Add($control)
            foreach($child in $control.Controls){$pending.Enqueue($child)}
        }
        $install=$controls | Where-Object {$_.AccessibleName -eq 'Install action'} | Select-Object -First 1
        $build=$controls | Where-Object {$_.AccessibleName -eq 'Detected build' -and $_.Text -match 'detected|Unknown game build'} | Select-Object -First 1
        if(-not $build){return}
        if($install.Enabled -ne $installAllowed){throw 'Install readiness must follow the backend build classification'}
        if($installAllowed){
            if($build.Text -ne 'EU 1.1 detected'){throw 'Display the build label from data'}
            $status=$controls | Where-Object {$_.AccessibleName -eq 'Detected game status'} | Select-Object -First 1
            if($installed){
                if($install.Text -notlike 'RE&INSTALL*' -or $status.Text -notlike 'Installed*reinstall*'){
                    throw 'Installed games must offer reinstall with a matching explanation'
                }
            } elseif($install.Text -notlike '&INSTALL*' -or $status.Text -ne 'Ready to install.'){
                throw 'New installs must have consistent readiness text'
            }
        }
        foreach($name in @('FOV','FrameCap')){
            $number=$controls | Where-Object {$_.AccessibleName -eq $name} | Select-Object -First 1
            if($number -isnot [Windows.Forms.NumericUpDown] -or $number.DecimalPlaces -ne 0){throw "$name needs an integer input"}
        }
        $fov=$controls | Where-Object {$_.AccessibleName -eq 'FOV'} | Select-Object -First 1
        $fov.Value=1
        if($fov.Value -ne 60){throw 'FOV must jump from zero to 60'}
        $fov.Value=59
        if($fov.Value -ne 0){throw 'FOV must jump from 60 to zero'}
        $dsoal=$controls | Where-Object {$_.AccessibleName -like 'DSOAL +*'} | Select-Object -First 1
        if(-not $dsoal -or $dsoal.Enabled){throw 'Missing package must disable DSOAL'}
        if(-not ($controls | Where-Object {$_.AccessibleName -eq 'Trace EAX'})){throw 'TraceEax diagnostic missing'}
        $desktop=$controls | Where-Object {$_.AccessibleName -eq 'Desktop mode'} | Select-Object -First 1
        if($desktop -isnot [Windows.Forms.ComboBox] -or $desktop.DropDownStyle -ne 'DropDownList' -or $desktop.FlatStyle -ne 'Flat' -or
            $desktop.Items[0] -ne 'Automatic (recommended)' -or $desktop.Items.Count -lt 2){
            throw 'Desktop mode must offer automatic, detected and custom modes'
        }
        foreach($check in @($controls | Where-Object {$_ -is [Windows.Forms.CheckBox]})){
            if(-not $check.AccessibleName -or -not $check.AccessibleDescription){throw 'Each checkbox needs a name and description'}
            if($check.AccessibilityObject.Role -ne 'CheckButton'){throw 'Custom paint must retain native checkbox accessibility'}
        }
        if($window.AcceptButton -ne $install -or $window.CancelButton.Text -ne 'E&xit'){
            throw 'Enter / Escape actions were lost'
        }
        foreach($name in @('Display','Graphics','Input','Audio','Diagnostics')){
            $section=$controls | Where-Object {$_.AccessibleName -eq "$name settings"} | Select-Object -First 1
            if(-not $section -or -not $section.TabStop){throw "$name section is not keyboard reachable"}
            $section.PerformClick()
            $page=$controls | Where-Object {$_.AccessibleName -eq "$name options"} | Select-Object -First 1
            if(-not $page.Visible){throw "$name section did not open"}
        }
        $log=$controls | Where-Object {$_.AccessibleName -eq 'Setup log'} | Select-Object -First 1
        $toggle=$controls | Where-Object {$_.AccessibleName -eq 'Toggle activity log'} | Select-Object -First 1
        if($log.Visible -or -not $toggle.TabStop){throw 'Activity log must start collapsed with a keyboard disclosure'}
        $toggle.PerformClick()
        if(-not $log.Visible){throw 'Activity log did not open'}
        $toggle.PerformClick()
        if($log.Visible){throw 'Activity log did not close'}
        $probe.Done=$true; $observer.Stop(); $window.Close()
    } catch {$probe.Failure=$_.Exception.Message; $observer.Stop(); $window.Dispose()}
})
$observer.Start()
try {
""" + (
        f"Show-BigFixSetup @{{GameDir={literal(game_dir)};DataFile={literal(data_file)};"
        f"RegistryFile={literal(registry_file)}}} ''"
    ) + r"""
} finally {$observer.Stop(); $observer.Dispose()}
if($probe.Failure){throw $probe.Failure}
if(-not $probe.Done){throw 'Configuration navigation was not exercised'}
"""
    result = ps(command, host)
    assert result.returncode == 0, result.stdout + result.stderr
    assert before == (tree_hashes(game_dir), registry_file.read_bytes())


@pytest.mark.parametrize("host", ["powershell", "pwsh"])
def test_window_preview_streams_backend_output_and_returns_to_idle(
    game_dir, source_dir, audio_data, registry_file, shortcut_dir, host,
):
    if not shutil.which(host) or not shutil.which("powershell"):
        pytest.skip("Windows and the selected PowerShell host are required")
    before = tree_hashes(game_dir), registry_file.read_bytes()
    # Drive real controls through the Windows message loop; every operation is WhatIf.
    # Load the form off-screen in this test process, without changing its event handlers.
    command = f". {literal(SETUP)}; " + r"""
Add-Type -AssemblyName System.Windows.Forms
$probe=@{Phase=0;Failure=''}; $watch=[Diagnostics.Stopwatch]::StartNew()
$observer=New-Object Windows.Forms.Timer; $observer.Interval=80
$observer.Add_Tick({
    $window=@([Windows.Forms.Application]::OpenForms | Where-Object {$_.Text -like '*BigFix*Setup*'}) | Select-Object -First 1
    if(-not $window){return}
    $window.Opacity=0
    $window.Location=New-Object Drawing.Point(-32000,-32000)
    $window.ClientSize=New-Object Drawing.Size(760,560)
    try {
        if($watch.Elapsed.TotalSeconds -gt 30){throw 'Window did not return to idle within 30 seconds'}
        $controls=New-Object 'Collections.Generic.List[Windows.Forms.Control]'
        $pending=New-Object Collections.Queue; $pending.Enqueue($window)
        while($pending.Count){
            $control=$pending.Dequeue(); $controls.Add($control)
            foreach($child in $control.Controls){$pending.Enqueue($child)}
        }
        $install=$controls | Where-Object {$_.AccessibleName -eq 'Install action'} | Select-Object -First 1
        $log=$controls | Where-Object {$_.AccessibleName -eq 'Setup log'} | Select-Object -First 1
        if($probe.Phase -eq 0 -and $install.Enabled){
            if(-not $window.HorizontalScroll.Visible -or -not $window.VerticalScroll.Visible){throw 'Small-window controls are not reachable by scrolling'}
            ($controls | Where-Object {$_.Text -eq '&Preview only (do not modify files)'}).Checked=$true
            ($controls | Where-Object {$_.AccessibleName -eq 'FOV'}).Value=90
            ($controls | Where-Object {$_.AccessibleName -eq 'Shader mode'}).SelectedIndex=1
            ($controls | Where-Object {$_.AccessibleName -like 'DSOAL +*'}).Checked=$true
            ($controls | Where-Object {$_.AccessibleName -eq 'Trace EAX'}).Checked=$true
            $probe.Phase=1; $install.PerformClick()
        } elseif($probe.Phase -eq 1 -and $install.Enabled -and $log.Text -match 'Backend finished with exit code 0'){
            if($log.Text -notmatch 'FOV=90' -or $log.Text -notmatch 'XR_SHADERMODE=6' -or $log.Text -notmatch 'SND_USEEAX=1' -or $log.Text -notmatch 'TraceEax=1'){throw 'Selected options did not reach backend output'}
            if(($controls | Where-Object {$_.AccessibleName -eq 'FOV'}).Value -ne 90 -or
                ($controls | Where-Object {$_.AccessibleName -eq 'Shader mode'}).SelectedIndex -ne 1){throw 'Preview discarded the selected options'}
            $probe.Phase=2; $observer.Stop(); $window.Close()
        }
    } catch {$probe.Failure=$_.Exception.Message; $observer.Stop(); $window.Dispose()}
})
$observer.Start()
try {
""" + (
        f"Show-BigFixSetup @{{GameDir={literal(game_dir)};SourceDir={literal(source_dir)};"
        f"DataFile={literal(audio_data)};RegistryFile={literal(registry_file)};"
        f"ShortcutDir={literal(shortcut_dir)};DesktopMode='2560x1440@180'}} ''"
    ) + r"""
} finally {$observer.Stop(); $observer.Dispose()}
if($probe.Failure){throw $probe.Failure}
if($probe.Phase -ne 2){throw 'Preview was not exercised'}
"""
    result = ps(command, host)
    assert result.returncode == 0, result.stdout + result.stderr
    assert before == (tree_hashes(game_dir), registry_file.read_bytes())

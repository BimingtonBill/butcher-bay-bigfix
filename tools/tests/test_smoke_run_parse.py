"""Parse and isolated helper tests for tools/smoke_run.ps1.

The smoke run launches the real game, so these tests only ever *parse* the
script and exercise helpers: they never execute its run body. It must parse under
PowerShell 7 (pwsh) and under Windows PowerShell 5.1, because a user may run it
either way.
"""

from __future__ import annotations

import os
import shutil
import subprocess
from pathlib import Path

import pytest

TOOLS_DIR = Path(__file__).resolve().parents[1]
SCRIPT = TOOLS_DIR / "smoke_run.ps1"

PARAMETERS = [
    "GameDir",
    "Seconds",
    "Restore",
    "PluginDir",
    "NoInstall",
    "LoaderDll",
    "CaptureKeyVk",
    "TraceVideo",
    "AntiAliasing",
    "TracePixelFormat",
    "TraceInput",
    "MouseBurst",
    "Patterns",
]


def discover_hosts() -> list[str]:
    hosts = []
    pwsh = shutil.which("pwsh")
    if pwsh:
        hosts.append(pwsh)
    system_root = os.environ.get("SystemRoot", r"C:\Windows")
    legacy = Path(system_root) / "System32" / "WindowsPowerShell" / "v1.0" / "powershell.exe"
    if legacy.is_file():
        hosts.append(str(legacy))
    return hosts


HOSTS = discover_hosts()


def ps_quote(text: str) -> str:
    return "'" + text.replace("'", "''") + "'"


def run_host(host: str, command: str) -> subprocess.CompletedProcess:
    return subprocess.run(
        [host, "-NoProfile", "-NonInteractive", "-ExecutionPolicy", "Bypass", "-Command", command],
        capture_output=True,
        text=True,
    )


@pytest.fixture(params=HOSTS or [None], ids=[Path(host).stem for host in HOSTS] or ["none"])
def host(request):
    if request.param is None:
        pytest.skip("no PowerShell host (pwsh or Windows PowerShell) found")
    return request.param


def test_parses_without_errors(host):
    command = (
        "$errors = $null; "
        f"[void][System.Management.Automation.Language.Parser]::ParseFile({ps_quote(str(SCRIPT))}, $null, [ref]$errors); "
        "if ($errors.Count -gt 0) { "
        "  $errors | ForEach-Object { $_.Extent.StartLineNumber.ToString() + ': ' + $_.Message } "
        "} else { 'PARSE-OK' }"
    )
    proc = run_host(host, command)
    assert proc.returncode == 0, proc.stderr
    assert "PARSE-OK" in proc.stdout, proc.stdout + proc.stderr


def test_help_lists_parameters(host):
    command = f"(Get-Help {ps_quote(str(SCRIPT))}).parameters.parameter.name"
    proc = run_host(host, command)
    assert proc.returncode == 0, proc.stderr
    missing = [name for name in PARAMETERS if name not in proc.stdout]
    assert not missing, f"missing from help under {Path(host).stem}: {missing}\n{proc.stdout}"


def test_dash_question_shows_help(host):
    # Windows PowerShell 5.1 prints nothing for `-File script.ps1 -?`, so ask
    # through the call operator; PowerShell still shows help instead of running.
    proc = run_host(host, f"& {ps_quote(str(SCRIPT))} -?")
    output = (proc.stdout or "") + (proc.stderr or "")
    assert proc.returncode == 0, output
    assert "GameDir" in output, output
    assert "AntiAliasing" in output, output
    assert "TracePixelFormat" in output, output



def load_helpers() -> str:
    # Evaluate only helper definitions and the pattern table from the parsed AST.
    # Never evaluate the script's setup/run body or launch a process.
    return (
        "$ErrorActionPreference = 'Stop'; $errors = $null; $tokens = $null; "
        f"$ast = [System.Management.Automation.Language.Parser]::ParseFile({ps_quote(str(SCRIPT))}, [ref]$tokens, [ref]$errors); "
        "$ast.FindAll({param($n) $n -is [System.Management.Automation.Language.FunctionDefinitionAst]}, $false) | "
        "ForEach-Object { Invoke-Expression $_.Extent.Text }; "
        "$table = $ast.FindAll({param($n) $n -is [System.Management.Automation.Language.AssignmentStatementAst] "
        "-and $n.Left.Extent.Text -eq '$defaultPatterns'}, $false) | Select-Object -First 1; "
        "$patterns = Invoke-Expression $table.Right.Extent.Text; "
    )


def test_msaa_ini_overrides_only_bigfix_settings(host, tmp_path):
    ini = tmp_path / 'BigFix.ini'
    ini.write_text('[Other]\nAntiAliasing=8\n[General]\nAntiAliasing=2\n'
                   '[BigFix]\nAntiAliasing=0\nTracePixelFormat=0\nCaptureKey=F9\n')
    command = load_helpers() + f"$ini = {ps_quote(str(ini))}; " + r"""
Set-MsaaOptions -Path $ini -AntiAliasing 4 -TracePixelFormat
$text = [System.IO.File]::ReadAllText($ini)
if ($text -notmatch '\[Other\]\r?\nAntiAliasing=8') { throw 'unrelated section changed' }
if ($text -match 'AntiAliasing=[02]') { throw 'conflicting AA setting retained' }
if ($text -notmatch 'AntiAliasing=4' -or $text -notmatch 'TracePixelFormat=1') { throw 'options not written' }
if ($text -notmatch 'CaptureKey=F9') { throw 'existing setting lost' }
Set-MsaaOptions -Path $ini -AntiAliasing 4 -TracePixelFormat
if ([System.IO.File]::ReadAllText($ini) -ne $text) { throw 'not idempotent' }
Set-MsaaOptions -Path $ini -AntiAliasing 0
if ([System.IO.File]::ReadAllText($ini) -match 'AntiAliasing=4') { throw 'cannot disable AA' }
'MSAA-INI-OK'
"""
    proc = run_host(host, command)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert 'MSAA-INI-OK' in proc.stdout


def test_first_frame_native_check(host):
    command = load_helpers() + r"""
$pattern = $patterns.FirstFrame
if (-not $pattern) { throw 'missing FirstFrame smoke pattern' }
foreach ($case in @(
    @('First frame presented: 2560x1440', '2560x1440@180', $true),
    @('First frame presented: 640x480', '2560x1440@180', $false),
    @('Borderless window style: ready', '2560x1440@180', $false),
    @('First frame presented: 0x0', '2560x1440@180', $false),
    @('First frame presented: 2560x1440', '', $false))) {
    $actual = Test-FirstFrameNative -LogText $case[0] -Pattern $pattern -DesktopMode $case[1]
    if ($actual -ne $case[2]) { throw ('incorrect first_frame_native: ' + $case[0]) }
}
'FIRST-FRAME-CHECK-OK'
"""
    proc = run_host(host, command)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert 'FIRST-FRAME-CHECK-OK' in proc.stdout, proc.stdout + proc.stderr


def test_waits_for_presented_frame_instead_of_borderless_window(host):
    command = load_helpers() + r"""
$script:reads = 0
function Read-LogText {
    param([string]$Path)
    $script:reads++
    if ($script:reads -eq 1) { return 'Borderless window style: ready' }
    return 'First frame presented: 2560x1440'
}
function Start-Sleep { param([int]$Milliseconds) }
$fake = [pscustomobject]@{ HasExited = $false }
$fake | Add-Member ScriptMethod Refresh {}
if (-not (Wait-FirstFrame -Process $fake -LogPath 'unused' -Pattern $patterns.FirstFrame -Seconds 1)) {
    throw 'did not observe frame'
}
if ($script:reads -ne 2) { throw 'wait ended on the window line' }
$fake.HasExited = $true
function Read-LogText { param([string]$Path) return 'Borderless window style: ready' }
if (Wait-FirstFrame -Process $fake -LogPath 'unused' -Pattern $patterns.FirstFrame -Seconds 1) {
    throw 'exited process incorrectly ready'
}
'WAIT-FIRST-FRAME-OK'
"""
    proc = run_host(host, command)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert 'WAIT-FIRST-FRAME-OK' in proc.stdout, proc.stdout + proc.stderr


@pytest.mark.parametrize('encoding', ['UTF8', 'Unicode'])
def test_first_frame_is_read_while_game_log_writer_is_still_open(host, tmp_path, encoding):
    command = load_helpers() + f"$path = {ps_quote(str(tmp_path / 'BigFix.log'))}; " + r"""
$writer = [System.IO.File]::Open($path, [System.IO.FileMode]::Create,
    [System.IO.FileAccess]::Write, [System.IO.FileShare]::ReadWrite)
try {
    $text = '[2026-10-29 15:12:48] First frame presented: 2560x1440' + "`r`n"
""" + f"    $bytes = [System.Text.Encoding]::{encoding}.GetBytes($text); " + r"""
    $writer.Write($bytes, 0, $bytes.Length); $writer.Flush()
    $fake = [pscustomobject]@{HasExited=$false}
    $fake | Add-Member ScriptMethod Refresh {}
    if (-not (Wait-FirstFrame -Process $fake -LogPath $path -Pattern $patterns.FirstFrame -Seconds 1)) {
        throw 'capture readiness missed FirstFrame while writer remained open'
    }
} finally { $writer.Dispose() }
'OPEN-LOG-OK'
"""
    # Include a BOM for the UTF-16 case, as StreamReader's detection requires it.
    command = command.replace('$writer.Write($bytes',
        f'$preamble = [System.Text.Encoding]::{encoding}.GetPreamble(); '
        '$writer.Write($preamble, 0, $preamble.Length); $writer.Write($bytes')
    proc = run_host(host, command)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert 'OPEN-LOG-OK' in proc.stdout


def test_modes_filtered_accepts_enumeration_or_native_borderless_selection(host):
    command = load_helpers() + r"""
foreach ($case in @(
    @('Display modes: 1200 -> 512 (cap=512)', $true, 'Display modes'),
    @('Display modes: 1200 -> 513 (cap=512)', $false, '513'),
    @('Borderless render selection: 640x480 format=40 @59 -> 2560x1440 32bpp @180 (fullscreen)', $true, 'Borderless render selection'),
    @('Borderless render selection: 640x480 format=40 @59 -> 2560x14400 32bpp @180 (fullscreen)', $false, ''),
    @('Borderless render selection: 2560x1440 format=40 @179 -> 640x480 32bpp @59 (fullscreen)', $false, ''),
    @("Display modes: 1200 -> 513 (cap=512)`nBorderless render selection: 640x480 format=40 @59 -> 2560x1440 32bpp @180 (fullscreen)", $true, 'Borderless render selection'),
    @('Borderless window style: ready', $false, ''))) {
    $result = Get-ModesFilteredCheck -LogText $case[0] -Patterns $patterns -DesktopMode '2560x1440@180'
    if ($result.Ok -ne $case[1] -or -not $result.Detail.Contains($case[2])) {
        throw ('incorrect modes_filtered: ' + $case[0] + ': ' + $result)
    }
}
$result = Get-ModesFilteredCheck -LogText 'Borderless render selection: 640x480 -> 2560x1440' -Patterns $patterns -DesktopMode ''
if ($result.Ok) { throw 'missing desktop accepted' }
'MODES-FILTERED-OK'
"""
    proc = run_host(host, command)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert 'MODES-FILTERED-OK' in proc.stdout, proc.stdout + proc.stderr


def test_capture_failure_explains_background_key_rejection(host):
    command = load_helpers() + r"""
$detail = Get-CaptureFailureDetail -LogText "Capture key down: foreground=0`nCapture failed: foreground window is not owned by this process" -DefaultDetail 'no new PNG'
if (-not $detail.Contains('Capture key down: foreground=0')) { throw 'background diagnostic missing' }
if (-not $detail.Contains('Capture failed: foreground window is not owned by this process')) { throw 'failure reason missing' }
$detail = Get-CaptureFailureDetail -LogText '' -DefaultDetail 'no new PNG'
if ($detail -ne 'no new PNG') { throw 'default detail lost' }
'CAPTURE-DETAIL-OK'
"""
    proc = run_host(host, command)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert 'CAPTURE-DETAIL-OK' in proc.stdout, proc.stdout + proc.stderr


def test_capture_wait_ignores_existing_png_and_stops_on_new_png(host, tmp_path):
    (tmp_path / 'BigFix_old.png').write_bytes(b'old')
    command = load_helpers() + f"$shots = {ps_quote(str(tmp_path))}; " + r"""
$script:waits = 0
function Start-Sleep {
    param([int]$Milliseconds)
    $script:waits++
    [System.IO.File]::WriteAllBytes((Join-Path $shots 'BigFix_new.png'), [byte[]]@(1, 2, 3))
}
if (-not (Wait-NewCapture -ShotDir $shots -ExistingShots @('BigFix_old.png') -Seconds 10)) { throw 'new PNG not observed' }
if ($script:waits -ne 1) { throw 'did not wait for a new screenshot' }
if (Wait-NewCapture -ShotDir $shots -ExistingShots @('BigFix_old.png', 'BigFix_new.png') -Seconds 0) { throw 'timeout incorrectly succeeded' }
'CAPTURE-WAIT-OK'
"""
    proc = run_host(host, command)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert 'CAPTURE-WAIT-OK' in proc.stdout, proc.stdout + proc.stderr


def test_input_api_encodes_f12_scan_code_and_native_input_layout(host):
    command = load_helpers() + r"""
Initialize-GameInput
$down = [BigFixFocus]::KeyInput(0x7B, $false)
$up = [BigFixFocus]::KeyInput(0x7B, $true)
if ($down.type -ne 1 -or $down.data.keyboard.wVk -ne 0x7B -or $down.data.keyboard.wScan -ne 0x58 -or $down.data.keyboard.dwFlags -ne 8) {
    throw 'incorrect F12 key down input'
}
if ($up.data.keyboard.wVk -ne 0x7B -or $up.data.keyboard.wScan -ne 0x58 -or $up.data.keyboard.dwFlags -ne 10) { throw 'incorrect F12 key up input' }
$size = [Runtime.InteropServices.Marshal]::SizeOf($down)
if ($size -ne $(if ([IntPtr]::Size -eq 8) { 40 } else { 28 })) { throw 'incorrect native INPUT size' }
'INPUT-ENCODING-OK'
"""
    proc = run_host(host, command)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert 'INPUT-ENCODING-OK' in proc.stdout, proc.stdout + proc.stderr


def test_trace_video_ini_key_is_opt_in_and_overrides_an_existing_zero(host, tmp_path):
    ini = tmp_path / 'BigFix.ini'
    ini.write_text('[BigFix]\nCaptureKey=F9\nTraceVideo=0\n', encoding='utf-8')
    command = load_helpers() + f"$ini = {ps_quote(str(ini))}; " + r"""
Enable-VideoTrace -Path $ini
$text = [System.IO.File]::ReadAllText($ini)
if ($text -notmatch 'CaptureKey=F9') { throw 'existing setting lost' }
if ($text -notmatch 'TraceVideo=1') { throw 'trace not enabled' }
if ($text -match 'TraceVideo=0') { throw 'conflicting trace setting retained' }
Enable-VideoTrace -Path $ini
if ([System.IO.File]::ReadAllText($ini) -ne $text) { throw 'trace update not idempotent' }
'TRACE-INI-OK'
"""
    proc = run_host(host, command)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert 'TRACE-INI-OK' in proc.stdout


def test_trace_input_overrides_existing_zero_without_changing_other_keys(host, tmp_path):
    ini = tmp_path / 'BigFix.ini'
    ini.write_text('[BigFix]\nRawMouse=0\nTraceInput=0\nCaptureKey=F9\n', encoding='utf-8')
    command = load_helpers() + f"$ini = {ps_quote(str(ini))}; " + r"""
Enable-InputTrace -Path $ini
$text = [System.IO.File]::ReadAllText($ini)
if ($text -notmatch 'TraceInput=1' -or $text -match 'TraceInput=0') { throw 'trace input not enabled' }
if ($text -notmatch 'RawMouse=0' -or $text -notmatch 'CaptureKey=F9') { throw 'unrelated key changed' }
Enable-InputTrace -Path $ini
if ([System.IO.File]::ReadAllText($ini) -ne $text) { throw 'input trace update not idempotent' }
'INPUT-TRACE-OK'
"""
    proc = run_host(host, command)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert 'INPUT-TRACE-OK' in proc.stdout


def test_mouse_burst_is_relative_balanced_and_does_not_send_in_encoding_test(host):
    # Build the exact native inputs used by the burst, without calling SendInput.
    command = load_helpers() + r"""
Initialize-GameInput
$moves = [BigFixFocus]::MouseBurstInputs()
if ($moves.Count -ne 2000) { throw 'burst must contain 2000 moves' }
$sumX = 0; $sumY = 0; $nonzero = 0
for ($i=0; $i -lt 2000; $i++) {
    $move = $moves[$i]
    if ($move.type -ne 0 -or $move.data.mouse.dwFlags -ne 0x2001) { throw 'move must be relative and uncoalesced' }
    $sumX += $move.data.mouse.dx; $sumY += $move.data.mouse.dy
    if ($move.data.mouse.dx -ne 0 -or $move.data.mouse.dy -ne 0) { $nonzero++ }
    if ($i -lt 1000) {
        $reverse = $moves[1999-$i]
        if ($move.data.mouse.dx -ne -$reverse.data.mouse.dx -or $move.data.mouse.dy -ne -$reverse.data.mouse.dy) {
            throw 'reverse circle does not undo forward circle'
        }
    }
}
if ($sumX -ne 0 -or $sumY -ne 0 -or $nonzero -lt 1900) { throw 'burst drifts or has no motion' }
$size = [Runtime.InteropServices.Marshal]::SizeOf($moves[0])
if ($size -ne $(if ([IntPtr]::Size -eq 8) { 40 } else { 28 })) { throw 'wrong mouse INPUT ABI' }
'MOUSE-BURST-OK'
"""
    proc = run_host(host, command)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert 'MOUSE-BURST-OK' in proc.stdout


@pytest.mark.parametrize('content', ['', '[Other]\nTraceVideo=0\n', '[General]\nTraceVideo=0\n[BigFix]\nTraceVideo=0\n'])
def test_trace_video_ini_handles_missing_key_or_other_section(host, tmp_path, content):
    ini = tmp_path / 'BigFix.ini'
    if content:
        ini.write_text(content, encoding='utf-8')
    command = load_helpers() + f"Enable-VideoTrace -Path {ps_quote(str(ini))}"
    proc = run_host(host, command)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    result = ini.read_text(encoding='utf-8')
    assert 'TraceVideo=1' in result
    if '[Other]' in content:
        assert '[BigFix]' in result
        assert '[Other]\nTraceVideo=0' in result


def test_smoke_staging_supplies_installer_ini_when_release_build_has_none(host, tmp_path):
    (tmp_path / 'BigFix.asi').write_bytes(b'fake ASI')
    (tmp_path / 'winmm.dll').write_bytes(b'fake loader')
    default_ini = TOOLS_DIR.parent / 'installer' / 'BigFix.default.ini'
    command = load_helpers() + (
        f"$stage = New-StagingDir -BuildPath {ps_quote(str(tmp_path))} "
        f"-LoaderDllPath {ps_quote(str(tmp_path / 'winmm.dll'))} "
        f"-DefaultIni {ps_quote(str(default_ini))}; "
        "try { if (-not (Test-Path -LiteralPath (Join-Path $stage 'BigFix.ini'))) { throw 'installer INI missing' }; "
        "'STAGE-INI-OK' } finally { Remove-Item -LiteralPath $stage -Recurse -Force }"
    )
    proc = run_host(host, command)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert 'STAGE-INI-OK' in proc.stdout


def test_menu_skip_waits_twenty_seconds_after_first_frame_then_sends_one_escape(host):
    command = load_helpers() + r"""
$script:waits = 0; $script:keys = @(); $script:focused = $true
function Start-Sleep { param([int]$Milliseconds) $script:waits += $Milliseconds }
function Set-GameForeground { param($Process) return $script:focused }
function Send-CaptureKey { param([int]$VirtualKey) $script:keys += $VirtualKey; return $true }
$fake = [pscustomobject]@{HasExited=$false}
$fake | Add-Member ScriptMethod Refresh {}
if (-not (Wait-IntroThenMenu -Process $fake)) { throw 'skip failed' }
if ($script:waits -ne 25000) { throw ('incorrect intro/menu wait: ' + $script:waits) }
if ($script:keys.Count -ne 1 -or $script:keys[0] -ne 0x1b) { throw 'expected one ESC only' }
$script:focused = $false; $script:keys = @()
if (Wait-IntroThenMenu -Process $fake) { throw 'unfocused skip accepted' }
if ($script:keys.Count -ne 0) { throw 'ESC sent to another app' }
'INTRO-SKIP-OK'
"""
    proc = run_host(host, command)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert 'INTRO-SKIP-OK' in proc.stdout
    assert 'ESC' in proc.stdout


def test_focus_verifies_actual_window_and_capture_holds_for_80ms(host):
    command = load_helpers() + r"""
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
public static class BigFixFocus {
    public static List<string> events = new List<string>();
    public static IntPtr foreground = new IntPtr(456);
    public static bool sendOk = true;
    public static bool AllowSetForegroundWindow(int pid) { events.Add("allow:" + pid); return true; }
    public static bool SendKey(int vk, bool up) { events.Add("key:" + vk + ":" + up); return sendOk; }
    public static bool ShowWindow(IntPtr h, int cmd) { events.Add("show:" + h); return true; }
    public static bool SetForegroundWindow(IntPtr h) { events.Add("focus:" + h); return true; }
    public static IntPtr GetForegroundWindow() { return foreground; }
}
'@
function Start-Sleep { param([int]$Milliseconds) [BigFixFocus]::events.Add('wait:' + $Milliseconds) }
$fake = [pscustomobject]@{ HasExited = $false; MainWindowHandle = [IntPtr]123; Id = 42 }
$fake | Add-Member ScriptMethod Refresh {}
if (Set-GameForeground -Process $fake) { throw 'SetForegroundWindow return accepted despite actual window mismatch' }
[BigFixFocus]::foreground = [IntPtr]123
if (-not (Set-GameForeground -Process $fake)) { throw 'actual foreground not accepted' }
if (-not ([BigFixFocus]::events -contains 'allow:42') -or -not ([BigFixFocus]::events -contains 'key:18:False') -or -not ([BigFixFocus]::events -contains 'key:18:True')) { throw 'foreground prerequisites missing' }
[BigFixFocus]::events.Clear()
if (-not (Send-CaptureKey -VirtualKey 0x7B)) { throw 'key send failed' }
if (([BigFixFocus]::events -join ',') -ne 'key:123:False,wait:80,key:123:True') { throw 'wrong capture key sequence' }
[BigFixFocus]::sendOk = $false
if (Send-CaptureKey -VirtualKey 0x7B) { throw 'SendInput failure not reported' }
'FOCUS-AND-KEY-OK'
"""
    proc = run_host(host, command)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    assert 'FOCUS-AND-KEY-OK' in proc.stdout, proc.stdout + proc.stderr

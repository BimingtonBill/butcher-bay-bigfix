"""Driver helpers/action blocks only: no real game process or input is used."""
import json
from pathlib import Path

import pytest
from test_smoke_run_parse import host, ps_quote, run_host

SCRIPT = Path(__file__).resolve().parents[1] / "game_driver.ps1"


def helpers():
    return (
        "$ErrorActionPreference='Stop'; $errors=$null; $tokens=$null; "
        f"$ast=[Management.Automation.Language.Parser]::ParseFile({ps_quote(str(SCRIPT))},[ref]$tokens,[ref]$errors); "
        "if($errors.Count){throw ($errors | Out-String)}; "
        "$ast.FindAll({param($n) $n -is [Management.Automation.Language.FunctionDefinitionAst]},$false) | "
        "ForEach-Object {Invoke-Expression $_.Extent.Text}; "
    )


def run(host, body):
    proc = run_host(host, helpers() + body)
    assert proc.returncode == 0, proc.stdout + proc.stderr
    return proc.stdout.strip()


def action(name):
    return (
        "$switch=$ast.FindAll({param($n) $n -is [Management.Automation.Language.SwitchStatementAst]},$false) | "
        "Select-Object -First 1; "
        f"$block=($switch.Clauses | Where-Object {{$_.Item1.Value -eq '{name}'}}).Item2; "
        "Invoke-Expression $block.Extent.Text.Substring(1,$block.Extent.Text.Length-2); "
    )


def test_driver_parameters_and_actions(host):
    out = run(host, "$ast.ParamBlock.Parameters.Name.VariablePath.UserPath; $ast.FindAll({param($n) "
              "$n -is [Management.Automation.Language.SwitchStatementAst]},$false)[0].Clauses.Item1.Value")
    for expected in ["Map", "ExtraArgs", "WaitFirstFrame", "burst", "look", "Dx", "Steps"]:
        assert expected in out


def test_extra_arguments_round_trip_windows_quoting(host):
    # CommandLineToArgvW checks spaces, embedded quotes, empty args, and trailing
    # backslashes independently of the driver's quoting implementation.
    body = r'''
Add-Type @'
using System;
using System.Runtime.InteropServices;
public class SplitArgs {
 [DllImport("shell32.dll", CharSet=CharSet.Unicode)] static extern IntPtr CommandLineToArgvW(string s,out int n);
 [DllImport("kernel32.dll")] static extern IntPtr LocalFree(IntPtr p);
 public static string[] Split(string s) {int n; var p=CommandLineToArgvW(s,out n);
 try {var a=new string[n]; for(int i=0;i<n;i++) a[i]=Marshal.PtrToStringUni(Marshal.ReadIntPtr(p,i*IntPtr.Size)); return a;}
 finally {LocalFree(p);} }
}
'@
$values=@('-exec','echo "two words"','', 'C:\a b\', 'x\\"y')
$line=Get-LaunchArguments -ExtraArgs $values
ConvertTo-Json -Compress -InputObject @([SplitArgs]::Split('game.exe '+$line) | Select-Object -Skip 1)
'''
    assert json.loads(run(host, body)) == ["-exec", 'echo "two words"', "", "C:\\a b\\", 'x\\\\"y']


def test_map_uses_frontend_quickmap_and_preserves_display_settings(host):
    out = run(host, r'''
$cfg=Get-QuickMapEnvironment -Content "VID_MODE=desktop`r`nQUICKMAP=old`r`nQUICKMAP=older`r`n" -Map 'Pa1_Arrival'
if($cfg -notmatch 'VID_MODE=desktop' -or ([regex]::Matches($cfg,'(?m)^QUICKMAP=')).Count -ne 1 -or
   $cfg -notmatch 'QUICKMAP=Pa1_Arrival') {throw 'incorrect quickmap config'}
foreach($bad in @('','a";quit','../Pa1_Intro',"x`nQUICKMENU=main")) {
 $rejected=$false; try {Get-QuickMapEnvironment '' $bad | Out-Null} catch {$rejected=$true}
 if(-not $rejected){throw 'invalid map accepted'}
}
'OK'
''')
    assert out == "OK"


def test_first_frame_ignores_stale_log_and_reports_timeout_or_exit(host):
    out = run(host, r'''
function Read-LogFrom($Offset) {if($Offset -ne 123){throw 'stale offset'}; 'First frame presented: 1920x1080'}
function Get-Game {return $true}
Wait-DriverFirstFrame -Offset 123 -TimeoutSec 1
function Read-LogFrom($Offset) {return ''}
$failed=$false; try {Wait-DriverFirstFrame -Offset 123 -TimeoutSec 0} catch {$failed=$true}
if(-not $failed){throw 'timeout succeeded'}
function Get-Game {return $null}
$failed=$false; try {Wait-DriverFirstFrame -Offset 123 -TimeoutSec 1} catch {$failed=$true}
if(-not $failed){throw 'exit succeeded'}
'OK'
''')
    assert out == "OK"


@pytest.mark.parametrize("ini,expected", [
    ("", 1), ("[Other]\nCaptureBurst=9\n[BigFix]\nCaptureBurst=3 ; comment", 3),
    ("[BigFix]\nCaptureBurst=4\n[General]\nCaptureBurst=2", 4),
    ("CaptureBurst=3\nCaptureBurst=17\nCaptureBurst=garbage", 3),
])
def test_capture_burst_count_matches_ini_rules(host, ini, expected):
    out = run(host, f"(Get-DriverCaptureSettings -Content {ps_quote(ini)}).Count")
    assert int(out) == expected


def test_burst_presses_once_and_prints_all_fresh_png_paths(host, tmp_path):
    ini = tmp_path / "BigFix.ini"
    ini.write_text("[BigFix]\nCaptureBurst=3\n")
    pngs = [tmp_path / f"burst {i}.png" for i in range(3)]
    for p in pngs:
        p.write_bytes(b"fixture")
    lines = "\n".join(f"Capture saved: {p} 2560x1440" for p in pngs)
    body = f"$ini={ps_quote(str(ini))}; $TimeoutSec=1; $script:lines={ps_quote(lines)}; " + r'''
$script:presses=0
function Focus-Game {}
function Log-Length {return 555}
function Press($Vk,$Hold) {if($Vk -ne 0x7b){throw 'not F12'}; $script:presses++}
function Read-LogFrom($Offset) {if($Offset -ne 555){throw 'stale capture'}; $script:lines}
function Get-Game {return $true}
''' + action("burst") + "if($script:presses -ne 1){throw 'multiple presses'}"
    assert run(host, body).splitlines() == [str(p) for p in pngs]


def test_burst_rejects_disabled_png_and_other_capture_key_before_input(host):
    out = run(host, r'''
foreach($text in @('CapturePng=0','CaptureKey=F11','CaptureKey=f011')) {
 $settings=Get-DriverCaptureSettings -Content $text
 if($settings.Png -and $settings.Key -eq 'F12'){throw 'invalid burst config accepted'}
}
function Read-LogFrom($Offset) {'Capture failed: previous PNG capture still busy'}
function Get-Game {return $true}
$failed=$false; try {Wait-DriverBurst -Offset 0 -Count 2 -TimeoutSec 1} catch {$failed=$true}
if(-not $failed){throw 'capture failure ignored'}
'OK'
''')
    assert out == "OK"


@pytest.mark.parametrize("wait", [True, False])
def test_launch_and_quit_in_scratch_tree_never_start_process(host, tmp_path, wait):
    body = f"$systemDir={ps_quote(str(tmp_path))}; $Map='Pa1_Intro'; $WaitFirstFrame=${str(wait).lower()}; " + r'''
$ini=Join-Path $systemDir 'BigFix.ini'; $environment=Join-Path $systemDir 'Environment.cfg'
$asi=Join-Path $systemDir 'BigFix.asi'; $log=Join-Path $systemDir 'BigFix.log'
$exe=Join-Path $systemDir 'SbzEngine.exe'; $outRoot=$systemDir; $backupDir=Join-Path $systemDir '.backup'
$Settings='CaptureBurst=3'; $ExtraArgs=@('-exec','echo "hello world"'); $ShaderMode=5; $TimeoutSec=1
$PluginPath=$null; $script:started=0; $script:waited=0; $script:focused=0
[IO.File]::WriteAllText($ini,"[BigFix]`nCaptureBurst=1`n")
[IO.File]::WriteAllText($environment,"VID_MODE=desktop`nQUICKMAP=old`nXR_SHADERMODE=2`n")
[IO.File]::WriteAllText($asi,'original plugin')
[IO.File]::WriteAllText($log,'First frame presented: 1x1')
function Get-Game {return $null}
function Focus-Game {$script:focused++}
function Wait-DriverFirstFrame($Offset,$TimeoutSec) {$script:waited++; if(Test-Path $log){throw 'stale log retained'}}
function Start-Process {
 param($FilePath,$WorkingDirectory,$ArgumentList,$WindowStyle,[switch]$PassThru)
 $script:started++
 if($FilePath -ne $exe -or $WorkingDirectory -ne $systemDir -or
    $ArgumentList -ne (Get-LaunchArguments $ExtraArgs)){throw 'bad launch arguments'}
 if([IO.File]::ReadAllText($environment) -notmatch 'QUICKMAP=Pa1_Intro' -or
    [IO.File]::ReadAllText($environment) -notmatch 'XR_SHADERMODE=5'){throw 'missing map/shader'}
 [pscustomobject]@{Id=12345}
}
''' + action("launch") + r'''
if($script:started -ne 1 -or $script:waited -ne [int]$WaitFirstFrame -or
   $script:focused -ne [int]$WaitFirstFrame){throw 'launch did extra work'}
if([IO.File]::ReadAllText((Join-Path $systemDir 'BigFix.prev.log')) -notmatch 'First frame'){throw 'old log lost'}
''' + action("quit") + r'''
if([IO.File]::ReadAllText($environment) -ne "VID_MODE=desktop`nQUICKMAP=old`nXR_SHADERMODE=2`n" -or
   [IO.File]::ReadAllText($ini) -ne "[BigFix]`nCaptureBurst=1`n" -or
   [IO.File]::ReadAllText($asi) -ne 'original plugin'){throw 'quit did not restore files'}
'OK'
'''
    assert run(host, body).endswith("OK")


def test_burst_requires_count_unique_existing_paths_and_has_timeout(host, tmp_path):
    p = tmp_path / "one.png"
    p.write_bytes(b"fixture")
    out = run(host, f"$script:one={ps_quote(str(p))}; " + r'''
function Get-Game {return $true}
function Read-LogFrom($Offset) {"Capture saved: $script:one 1x1`nCapture saved: $script:one 1x1"}
$failed=$false; try {Wait-DriverBurst -Offset 0 -Count 2 -TimeoutSec 0} catch {$failed=$true}
if(-not $failed){throw 'duplicate capture counted twice'}
Remove-Item -LiteralPath $script:one
$failed=$false; try {Wait-DriverBurst -Offset 0 -Count 1 -TimeoutSec 0} catch {$failed=$true}
if(-not $failed){throw 'nonexistent PNG accepted'}
'OK'
''')
    assert out == "OK"

<#
.SYNOPSIS
Run an x64-only bloom trace candidate and archive F12 PNGs, log and shader dumps.
.DESCRIPTION
Requires this build of BigFix.asi and an x64 winmm.dll loader already installed.
Defaults to map Pa1_Intro, the opening ship candidate (not yet runtime verified).
Use -Action Play -Scene panoptical01 for a repeatable shipped rendering baseline.
Never launches the protected x86 executables.
Preserves BigFix.ini and Environment.cfg byte-for-byte, including absence.
-DryRun prints the planned command without writes, input, or process launch.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][string]$GameDir,
    [ValidateSet('Play','Benchmark','Map','LoadGame')][string]$Action='Map',
    [ValidatePattern('^[A-Za-z0-9_.-]+$')][string]$Scene='Pa1_Intro',
    [ValidateSet(1,2,4,5,6)][int]$ShaderMode=5,
    [ValidateRange(1,120)][int[]]$CaptureSeconds=@(10,25,40),
    [ValidateRange(5,180)][int]$ReadyTimeout=60,
    [string]$OutputDir,
    [switch]$DryRun
)
$ErrorActionPreference='Stop'
$gameRoot=(Resolve-Path -LiteralPath $GameDir).Path
$systemDir=Join-Path $gameRoot 'System\Win64_AMD64'
$exe=Join-Path $systemDir 'SbzEngine.exe'
if(-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Missing x64 executable: $exe" }
# Inspect PE as bytes; never infer bitness from filename alone.
$bytes=[IO.File]::ReadAllBytes($exe)
if($bytes.Length -lt 64 -or $bytes[0] -ne 0x4D -or $bytes[1] -ne 0x5A) { throw 'Expected AMD64 PE executable' }
$peOffset=[BitConverter]::ToInt32($bytes,0x3C)
if($peOffset -lt 0 -or $peOffset -gt $bytes.Length-6 -or
   [BitConverter]::ToUInt32($bytes,$peOffset) -ne 0x4550 -or
   [BitConverter]::ToUInt16($bytes,$peOffset+4) -ne 0x8664) { throw 'Refusing non-AMD64 executable' }
if($Action -in @('Play','Benchmark')) {
    $demo=Join-Path $gameRoot ('Content\Demos\'+$Scene+'.XDM')
    if(-not (Test-Path -LiteralPath $demo -PathType Leaf)) { throw "Missing demo: $demo" }
}
if($Action -eq 'Map') {
    $world=Join-Path $gameRoot ('Content\Worlds\'+$Scene+'.XW')
    if(-not (Test-Path -LiteralPath $world -PathType Leaf)) { throw "Missing world: $world" }
}
if(-not $OutputDir) {
    $OutputDir=Join-Path $PSScriptRoot ('..\local\bloom-'+$Scene+'-sm'+$ShaderMode+'-'+(Get-Date -Format 'yyyyMMdd-HHmmss-fff'))
}
$OutputDir=[IO.Path]::GetFullPath($OutputDir)
$runCfg=Join-Path $OutputDir 'run.cfg'
$arguments='-env "{0}" -exec "{1} \"{2}\""' -f $runCfg,$Action.ToLowerInvariant(),$Scene
$plan=[ordered]@{Executable=$exe;WorkingDirectory=$systemDir;Arguments=$arguments;ShaderMode=$ShaderMode;CaptureSeconds=$CaptureSeconds;OutputDir=$OutputDir}
if($DryRun) { $plan | ConvertTo-Json -Depth 4; return }
if(Get-Process -Name SbzEngine -ErrorAction SilentlyContinue) { throw 'Close the existing game before running bloom_scene.ps1' }
foreach($file in @('BigFix.asi','winmm.dll')) {
    if(-not (Test-Path -LiteralPath (Join-Path $systemDir $file))) { throw "Install $file in $systemDir first" }
}
if(Test-Path -LiteralPath $OutputDir) { throw 'OutputDir already exists; use a fresh directory for each run' }
New-Item -ItemType Directory -Path $OutputDir | Out-Null
$plan | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $OutputDir 'run.json') -Encoding utf8
$ini=Join-Path $systemDir 'BigFix.ini'
$cfg=Join-Path $gameRoot 'Environment.cfg'
$saved=@{}
foreach($path in @($ini,$cfg)) {
    $saved[$path]=$null
    if(Test-Path -LiteralPath $path) {
        $saved[$path]=[IO.File]::ReadAllBytes($path)
        [IO.File]::WriteAllBytes((Join-Path $OutputDir ([IO.Path]::GetFileName($path)+'.before')),$saved[$path])
    }
}
function Read-SharedLog([string]$Path) {
    if(-not (Test-Path -LiteralPath $Path)) { return '' }
    $stream=$null; $reader=$null
    try {
        $stream=[IO.File]::Open($Path,'Open','Read','ReadWrite')
        $reader=[IO.StreamReader]::new($stream)
        return $reader.ReadToEnd()
    } catch { return '' }
    finally { if($reader) {$reader.Dispose()} elseif($stream) {$stream.Dispose()} }
}
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BigFixBloomInput {
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr window, int command);
    [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
}
'@
$log=Join-Path $systemDir 'BigFix.log'
$shots=Join-Path $gameRoot 'ScreenShots'
$existing=@{}
Get-ChildItem -LiteralPath $shots -Filter 'BigFix_*.png' -ErrorAction SilentlyContinue | ForEach-Object { $existing[$_.Name]=$true }
$proc=$null; $captureCount=0; $started=Get-Date
try {
    # Appending a canonical section overrides prior recognized duplicate keys.
    $iniText=if($saved[$ini]) {[Text.Encoding]::UTF8.GetString($saved[$ini])} else {''}
    [IO.File]::WriteAllText($ini,$iniText+"`r`n[BigFix]`r`nTraceBloom=1`r`nDumpShaders=1`r`nCapturePng=1`r`nCaptureKey=F12`r`n",[Text.UTF8Encoding]::new($false))
    $cfgText=if($saved[$cfg]) {[Text.Encoding]::Default.GetString($saved[$cfg])} else {''}
    [IO.File]::WriteAllText($runCfg,$cfgText+"`r`nXR_SHADERMODE=$ShaderMode`r`n",[Text.Encoding]::Default)
    # Capture filenames read Environment.cfg. Keep that requested-mode label in
    # sync with -env, then restore the original bytes in the outer finally.
    [IO.File]::WriteAllText($cfg,$cfgText+"`r`nXR_SHADERMODE=$ShaderMode`r`n",[Text.Encoding]::Default)
    if(Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination (Join-Path $OutputDir 'BigFix.before.log') }
    $started=Get-Date
    $proc=Start-Process -FilePath $exe -WorkingDirectory $systemDir -ArgumentList $arguments -PassThru
    $deadline=(Get-Date).AddSeconds($ReadyTimeout)
    $ready=$false
    while((Get-Date) -lt $deadline) {
        $proc.Refresh()
        if($proc.HasExited) { throw 'Game exited before readiness' }
        # Require a fresh log with both the new observer and first presented frame.
        $text=Read-SharedLog $log
        if((Get-Item -LiteralPath $log -ErrorAction SilentlyContinue).LastWriteTime -ge $started -and
           $text -match 'TraceBloom enabled:' -and $text -match 'TraceBloom GL observation hooks created=(6|26)\b' -and
           $text -match 'First frame presented:') { $ready=$true; break }
        Start-Sleep -Milliseconds 250
    }
    if(-not $ready) { throw 'Timed out waiting for TraceBloom and first frame; inspect archived log' }
    $clock=[Diagnostics.Stopwatch]::StartNew()
    foreach($second in ($CaptureSeconds | Sort-Object -Unique)) {
        while($clock.Elapsed.TotalSeconds -lt $second) {
            $proc.Refresh(); if($proc.HasExited) { throw 'Game exited before requested captures' }
            Start-Sleep -Milliseconds 100
        }
        $proc.Refresh(); $window=$proc.MainWindowHandle
        if($window -eq [IntPtr]::Zero) { throw 'Game window unavailable for F12' }
        [void][BigFixBloomInput]::ShowWindow($window,9)
        [void][BigFixBloomInput]::SetForegroundWindow($window)
        Start-Sleep -Milliseconds 300
        if([BigFixBloomInput]::GetForegroundWindow() -ne $window) { throw 'Game is not foreground; refusing to send F12 elsewhere' }
        try {
            [BigFixBloomInput]::keybd_event(0x7B,0x58,0,[UIntPtr]::Zero)
            Start-Sleep -Milliseconds 150
        } finally { [BigFixBloomInput]::keybd_event(0x7B,0x58,2,[UIntPtr]::Zero) }
        $shotDeadline=(Get-Date).AddSeconds(12)
        $new=@()
        while((Get-Date) -lt $shotDeadline) {
            # Existence alone can mean the PNG worker has only opened the file.
            # Its saved message is written after encoding and fclose succeed.
            $savedLog=Read-SharedLog $log
            $new=@(Get-ChildItem -LiteralPath $shots -Filter 'BigFix_*.png' -ErrorAction SilentlyContinue |
                Where-Object {-not $existing.ContainsKey($_.Name) -and
                    $savedLog -match ('Capture saved: [^\r\n]*'+[regex]::Escape($_.Name))})
            if($new.Count) { break }
            Start-Sleep -Milliseconds 250
        }
        if(-not $new.Count) { throw "No new PNG for F12 at second $second" }
        foreach($shot in $new) { $existing[$shot.Name]=$true; ++$captureCount }
        Add-Content -LiteralPath (Join-Path $OutputDir 'captures.txt') -Value ("{0:o} requestedSecond={1} files={2}" -f (Get-Date),$second,($new.Name -join ','))
    }
} finally {
    try {
        if($proc -and -not $proc.HasExited) {
            [void]$proc.CloseMainWindow()
            if(-not $proc.WaitForExit(10000)) { $proc.Kill(); $proc.WaitForExit() }
        }
        if(Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination (Join-Path $OutputDir 'BigFix.log') }
        # Copy only this run's screenshots, after the asynchronous PNG worker exits.
        Get-ChildItem -LiteralPath $shots -Filter 'BigFix_*.png' -ErrorAction SilentlyContinue |
            Where-Object {$_.LastWriteTime -ge $started} | Copy-Item -Destination $OutputDir
        $dump=Join-Path $systemDir 'shader_dump'
        if(Test-Path -LiteralPath $dump) { Copy-Item -LiteralPath $dump -Destination (Join-Path $OutputDir 'shader_dump') -Recurse }
    } finally {
        foreach($path in @($ini,$cfg)) {
            if($null -ne $saved[$path]) { [IO.File]::WriteAllBytes($path,$saved[$path]) }
            elseif(Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path }
        }
    }
}
Write-Host "Captured $captureCount PNG(s); artifacts: $OutputDir"

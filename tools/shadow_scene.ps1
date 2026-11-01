<#
.SYNOPSIS
Capture a lit, Eyeshine-capable save in modes 5 and 6. Never run by unit tests.
.DESCRIPTION
Requires an installed x64 ASI loader and BigFix. Uses -env/-exec, then the
verified console command 'cmd togglenightvision'. The default Checkpoint is
only a candidate: early saves do not grant Eyeshine. Interactive verification
is deliberate; use -Unattended only with a known lit save starting Eyeshine OFF.
F11 arms TraceShadowPass for four frames. F12 takes three PNGs per state.
Restores BigFix.ini, Environment.cfg and an optionally replaced BigFix.asi.
Does not install a loader or disable another plugin. Refuses active TCoRFix.
.EXAMPLE
pwsh tools/shadow_scene.ps1 -GameDir D:\Games\Riddick -SaveName Checkpoint -PluginPath build\Release\BigFix.asi
.EXAMPLE
pwsh tools/shadow_scene.ps1 -GameDir D:\Games\Riddick -SceneCommand 'play "sodini01"'
#>
[CmdletBinding()]
param(
    [string]$GameDir = $env:RIDDICK_DIR,
    [ValidateSet(5,6)][int[]]$Modes = @(5,6),
    [string]$SaveName = 'Checkpoint',
    [string]$SceneCommand,
    [string]$PluginPath,
    [string]$OutputDir = (Join-Path $PSScriptRoot ('..\local\shadow-' + (Get-Date -Format yyyyMMdd-HHmmss))),
    [ValidateRange(5,180)][int]$LoadSeconds = 30,
    # Startup -exec runs before the title screen accepts a save; this presses
    # Enter at the title and types SceneCommand into the console instead.
    [switch]$ConsoleScene,
    [ValidateRange(5,300)][int]$SceneSeconds = 60,
    [switch]$NativeNv,
    [switch]$Workaround,
    [switch]$Unattended,
    [switch]$PlanOnly
)
$ErrorActionPreference = 'Stop'

function New-ShadowScenePlan {
    param([string]$Root, [int]$Mode, [string]$Command, [string]$Config)
    if ($Command -match '[\r\n]') { throw 'SceneCommand must be one console command line.' }
    # Windows CRT quoting: the benchmark launchers use \" inside -exec too.
    $escaped = $Command -replace '(\\*)"', '$1$1\"' -replace '(\\+)$', '$1$1'
    [pscustomobject]@{
        Mode = $Mode
        Executable = Join-Path $Root 'System\Win64_AMD64\SbzEngine.exe'
        WorkingDirectory = Join-Path $Root 'System\Win64_AMD64'
        Arguments = if ($ConsoleScene) { '-env "{0}"' -f $Config } else { '-env "{0}" -exec "{1}"' -f $Config, $escaped }
        Scene = $Command
        Toggle = 'cmd togglenightvision'
    }
}
if (-not $SceneCommand) {
    if ($SaveName -match '["\r\n;]') { throw 'SaveName must be a simple save name.' }
    $SceneCommand = 'loadgame "{0}"' -f $SaveName
}
if (-not $GameDir) { throw 'Pass -GameDir or set RIDDICK_DIR to the game folder.' }
$gameRoot = [IO.Path]::GetFullPath($GameDir)
$outRoot = [IO.Path]::GetFullPath($OutputDir)
if ($PlanOnly) {
    foreach ($mode in $Modes) { New-ShadowScenePlan $gameRoot $mode $SceneCommand (Join-Path $outRoot "mode$mode.cfg") }
    return
}
$systemDir = Join-Path $gameRoot 'System\Win64_AMD64'
$exe = Join-Path $systemDir 'SbzEngine.exe'
$ini = Join-Path $systemDir 'BigFix.ini'
$asi = Join-Path $systemDir 'BigFix.asi'
$environment = Join-Path $gameRoot 'Environment.cfg'
if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) { throw "Missing x64 executable: $exe" }
if (Get-Process SbzEngine,Riddick -ErrorAction SilentlyContinue) { throw 'Close the existing game before running this script.' }
if (Get-ChildItem -LiteralPath $systemDir -Filter '*TCoRFix*.asi') { throw 'Move TCoRFix aside before this controlled run.' }
if (-not (Test-Path -LiteralPath (Join-Path $systemDir 'winmm.dll'))) { throw 'Install the x64 ASI loader first.' }
if (-not $PluginPath -and -not (Test-Path -LiteralPath $asi)) { throw 'Install BigFix first or supply -PluginPath.' }
New-Item -ItemType Directory -Path $outRoot -Force | Out-Null

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ShadowSceneInput {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint type);
    [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] data, int size);
    [StructLayout(LayoutKind.Sequential)] struct KEY { public ushort vk, scan; public uint flags, time; public UIntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] struct MOUSE { public int x,y; public uint data,flags,time; public UIntPtr extra; }
    [StructLayout(LayoutKind.Explicit)] struct UNION { [FieldOffset(0)] public KEY key; [FieldOffset(0)] public MOUSE mouse; }
    [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public UNION data; }
    public static void Key(int vk, bool up) {
        uint scan=MapVirtualKey((uint)vk,4);
        INPUT i=new INPUT(); i.type=1; i.data.key.vk=(ushort)vk; i.data.key.scan=(ushort)(scan & 255);
        i.data.key.flags=(scan!=0 ? 8u:0u) | (up?2u:0u) | ((scan & 0xff00)==0xe000 ? 1u:0u);
        if(SendInput(1,new INPUT[]{i},Marshal.SizeOf(typeof(INPUT)))!=1) throw new Exception("SendInput failed");
    }
}
'@
function Focus-Scene {
    $script:game.Refresh()
    $handle = $script:game.MainWindowHandle
    if ($handle -eq [IntPtr]::Zero) { throw 'The launched game has no main window.' }
    [void][ShadowSceneInput]::ShowWindow($handle,9)
    [void][ShadowSceneInput]::SetForegroundWindow($handle)
    Start-Sleep -Milliseconds 400
    if ([ShadowSceneInput]::GetForegroundWindow() -ne $handle) { throw 'Game is not foreground; no input sent.' }
}
function Press-SceneKey([int]$Vk, [int]$HoldMs = 100) {
    if ([ShadowSceneInput]::GetForegroundWindow() -ne $script:game.MainWindowHandle) { throw 'Game lost focus; no input sent.' }
    [ShadowSceneInput]::Key($Vk,$false)
    try { Start-Sleep -Milliseconds $HoldMs } finally { [ShadowSceneInput]::Key($Vk,$true) }
    Start-Sleep -Milliseconds 100
}
function Toggle-SceneConsole {
    [ShadowSceneInput]::Key(0x11,$false)
    try {
        [ShadowSceneInput]::Key(0x12,$false)
        try { Press-SceneKey 0xC0 } finally { [ShadowSceneInput]::Key(0x12,$true) }
    } finally { [ShadowSceneInput]::Key(0x11,$true) }
    Start-Sleep -Milliseconds 400
}
function Type-SceneText([string]$Text) {
    foreach ($char in $Text.ToCharArray()) {
        switch ($char) {
            '"' { [ShadowSceneInput]::Key(0x10,$false); try { Press-SceneKey 0xDE } finally { [ShadowSceneInput]::Key(0x10,$true) } }
            '_' { [ShadowSceneInput]::Key(0x10,$false); try { Press-SceneKey 0xBD } finally { [ShadowSceneInput]::Key(0x10,$true) } }
            '.' { Press-SceneKey 0xBE }
            default { Press-SceneKey ([int][char]::ToUpperInvariant($char)) }
        }
    }
}
function Enter-ConsoleScene([string]$Command) {
    Focus-Scene
    Press-SceneKey 0x0D
    Start-Sleep -Seconds 5
    Toggle-SceneConsole
    Type-SceneText $Command
    Press-SceneKey 0x0D
    Toggle-SceneConsole
    Start-Sleep -Seconds $SceneSeconds
}
function Toggle-Eyeshine {
    Focus-Scene
    Toggle-SceneConsole
    Type-SceneText 'cmd togglenightvision'
    Press-SceneKey 0x0D
    Toggle-SceneConsole
    Start-Sleep -Seconds 2
}
function Capture-Scene([string]$Label, [string]$Destination) {
    Focus-Scene
    Press-SceneKey 0x7A # F11: four trace frames, separate from screenshot readback.
    Start-Sleep -Milliseconds 250
    $shots = Join-Path $gameRoot 'ScreenShots'
    for ($index=0; $index -lt 3; $index++) {
        $before = @(Get-ChildItem -LiteralPath $shots -Filter '*.png' -ErrorAction SilentlyContinue | ForEach-Object FullName)
        # Held past the game's own TGA write, which stalls a gameplay frame.
        Press-SceneKey 0x7B 1000
        $until = (Get-Date).AddSeconds(10)
        $new = @()
        do {
            Start-Sleep -Milliseconds 200
            $new = @(Get-ChildItem -LiteralPath $shots -Filter '*.png' -ErrorAction SilentlyContinue | Where-Object { $_.FullName -notin $before })
        } while (-not $new -and (Get-Date) -lt $until)
        if (-not $new) { throw "F12 produced no PNG for $Label; see BigFix.log." }
        # Allow the worker to close the file before copying it.
        Start-Sleep -Milliseconds 300
        Copy-Item -LiteralPath $new[0].FullName -Destination (Join-Path $Destination "$Label-$index.png")
    }
}
$backups = @{}
foreach ($path in @($ini,$environment) + $(if ($PluginPath) { @($asi) } else { @() })) {
    $backups[$path] = if (Test-Path -LiteralPath $path) { [IO.File]::ReadAllBytes($path) } else { $null }
}
$script:game = $null
try {
    if ($PluginPath) { Copy-Item -LiteralPath $PluginPath -Destination $asi -Force }
    foreach ($mode in $Modes) {
        $destination = Join-Path $outRoot "mode$mode"
        New-Item -ItemType Directory -Path $destination -Force | Out-Null
        $config = Join-Path $destination 'scene.cfg'
        $baseConfig = if ($null -ne $backups[$environment]) { [Text.Encoding]::Default.GetString($backups[$environment]) } else { '' }
        $baseConfig = $baseConfig -replace '(?im)^\s*XR_SHADERMODE\s*=.*(?:\r?\n|$)',''
        [IO.File]::WriteAllText($config, "$baseConfig`r`nXR_SHADERMODE=$mode`r`n", [Text.Encoding]::ASCII)
        # Controlled diagnostic settings; original bytes are restored in finally.
        [IO.File]::WriteAllText($ini, "[BigFix]`r`nTraceShadowPass=1`r`nDumpShaders=1`r`nCaptureKey=F12`r`nCapturePng=1`r`nWindowMode=borderless`r`nFrameCap=60`r`nAntiAliasing=0`r`nEnable20PlusPlusAnyGpu=$([int](-not $NativeNv))`r`nEyeshineFlickerWorkaround=$([int][bool]$Workaround)`r`n", [Text.Encoding]::ASCII)
        $plan = New-ShadowScenePlan $gameRoot $mode $SceneCommand $config
        $plan | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'launch.json')
        Copy-Item -LiteralPath $ini -Destination (Join-Path $destination 'BigFix.ini')
        Write-Host "Mode $mode : $($plan.Executable) $($plan.Arguments)"
        $script:game = Start-Process -FilePath $plan.Executable -WorkingDirectory $plan.WorkingDirectory -ArgumentList $plan.Arguments -PassThru
        Start-Sleep -Seconds $LoadSeconds
        if ($script:game.HasExited) { throw "Game exited before mode $mode capture." }
        if ($ConsoleScene) { Enter-ConsoleScene $SceneCommand }
        if (-not $Unattended) {
            [void](Read-Host "Verify shader mode $mode in video settings, save loaded in a LIT area, Eyeshine unlocked and OFF, console closed. Check character shadows and navigate if needed, then Enter")
        }
        Capture-Scene 'off' $destination
        Toggle-Eyeshine
        if (-not $Unattended) { [void](Read-Host 'Verify Eyeshine is ON and console closed. If toggle was rejected, use an unlocked save. Enter to capture') }
        Capture-Scene 'on' $destination
        Focus-Scene
        [void]$script:game.CloseMainWindow()
        if (-not $script:game.WaitForExit(15000)) { throw 'Game did not close; ending only the process launched by this script.' }
        Copy-Item -LiteralPath (Join-Path $systemDir 'BigFix.log') -Destination $destination
        $dump = Join-Path $systemDir 'shader_dump'
        if (Test-Path -LiteralPath $dump) { Copy-Item -LiteralPath $dump -Destination $destination -Recurse }
        $script:game = $null
    }
} finally {
    if ($script:game -and -not $script:game.HasExited) { Stop-Process -Id $script:game.Id; $script:game.WaitForExit() }
    foreach ($path in $backups.Keys) {
        if ($null -ne $backups[$path]) { [IO.File]::WriteAllBytes($path,$backups[$path]) }
        elseif (Test-Path -LiteralPath $path) { Remove-Item -LiteralPath $path -Force }
    }
}
Write-Host "Captures, launch settings, shader dumps and logs: $outRoot"

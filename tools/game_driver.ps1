<#
.SYNOPSIS
Step-by-step game driver for in-game checks: each call does one action and exits,
so the caller can look at a capture and decide the next step.
.DESCRIPTION
  launch [-Map Pa1_Intro] [-ExtraArgs @('-exec','command')] [-WaitFirstFrame]
         [-Settings 'Key=Value;...'] [-ShaderMode N] [-PluginPath asi]
                 backs up BigFix.ini/Environment.cfg/BigFix.asi, writes test settings, starts SbzEngine.exe
                 Map uses the frontend's QUICKMAP environment setting (no menus/profile).
                 WaitFirstFrame waits for a fresh first-present log, not level readiness.
  click                                     left mouse click (skips cutscenes; Esc opens the pause menu)
  look -Dx N [-Dy N] [-Steps 20]            relative mouse move in small steps (turns the camera)
  key -Keys 'ESC,ENTER,F11' [-HoldMs 100]   presses keys in order (game must be foreground)
  console -Text 'loadgame "Checkpoint"'    opens the console (Ctrl+Alt+~), types, Enter, closes it
  shot [-Name label] [-OutDir dir]          holds F12, waits for the BigFix PNG, copies it, prints its path
  burst [-TimeoutSec N]                    presses F12 once, waits for CaptureBurst PNGs, prints their original paths
  wait-log -Pattern regex [-TimeoutSec N]   waits for a new BigFix.log line matching regex
  status                                    process, foreground, last log lines
  quit                                      closes the game, restores backed-up files, keeps the log in OutDir
Input is only sent when the game window is foreground; otherwise the call fails without sending.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position=0)][ValidateSet('launch','key','click','look','console','shot','burst','wait-log','status','quit')][string]$Action,
    [string]$GameDir = $env:RIDDICK_DIR,
    [string]$Settings = '',
    [ValidateSet(0,1,2,4,5,6)][int]$ShaderMode = 0,
    [string]$PluginPath,
    [string]$Map,
    [string[]]$ExtraArgs = @(),
    [switch]$WaitFirstFrame,
    [string]$Keys,
    [int]$HoldMs = 100,
    [string]$Text,
    [string]$Name = 'shot',
    [string]$OutDir = (Join-Path $PSScriptRoot '..\local\drive'),
    [string]$Pattern,
    [int]$TimeoutSec = 60,
    [int]$Dx = 0,
    [int]$Dy = 0,
    [int]$Steps = 20
)
$ErrorActionPreference = 'Stop'
if (-not $GameDir) { throw 'Pass -GameDir or set RIDDICK_DIR to the game folder.' }
$gameRoot = [IO.Path]::GetFullPath($GameDir)
$systemDir = Join-Path $gameRoot 'System\Win64_AMD64'
$exe = Join-Path $systemDir 'SbzEngine.exe'
$ini = Join-Path $systemDir 'BigFix.ini'
$asi = Join-Path $systemDir 'BigFix.asi'
$log = Join-Path $systemDir 'BigFix.log'
$environment = Join-Path $gameRoot 'Environment.cfg'
$outRoot = [IO.Path]::GetFullPath($OutDir)
$backupDir = Join-Path $outRoot '.backup'
New-Item -ItemType Directory -Path $outRoot -Force | Out-Null

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class DriverInput {
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int n);
    [DllImport("user32.dll")] public static extern bool AllowSetForegroundWindow(int pid);
    [DllImport("user32.dll")] static extern uint MapVirtualKey(uint code, uint type);
    [DllImport("user32.dll")] static extern uint SendInput(uint n, INPUT[] data, int size);
    [StructLayout(LayoutKind.Sequential)] struct KEY { public ushort vk, scan; public uint flags, time; public UIntPtr extra; }
    [StructLayout(LayoutKind.Sequential)] struct MOUSE { public int x,y; public uint data,flags,time; public UIntPtr extra; }
    [StructLayout(LayoutKind.Explicit)] struct UNION { [FieldOffset(0)] public KEY key; [FieldOffset(0)] public MOUSE mouse; }
    [StructLayout(LayoutKind.Sequential)] struct INPUT { public uint type; public UNION data; }
    public static void Move(int dx, int dy, int steps) {
        // Relative motion split into steps so raw/DirectInput readers see a turn, not one jump.
        int sentX=0, sentY=0;
        for (int k=1; k<=steps; ++k) {
            int x=dx*k/steps-sentX, y=dy*k/steps-sentY; sentX+=x; sentY+=y;
            INPUT i=new INPUT(); i.type=0; i.data.mouse.x=x; i.data.mouse.y=y; i.data.mouse.flags=1u;
            if(SendInput(1,new INPUT[]{i},Marshal.SizeOf(typeof(INPUT)))!=1) throw new Exception("SendInput failed");
            System.Threading.Thread.Sleep(15);
        }
    }
    public static void Click() {
        // Left button down/up with no movement; skips cutscenes (Esc opens the pause menu).
        foreach (uint flag in new uint[]{2u, 4u}) {
            INPUT i=new INPUT(); i.type=0; i.data.mouse.flags=flag;
            if(SendInput(1,new INPUT[]{i},Marshal.SizeOf(typeof(INPUT)))!=1) throw new Exception("SendInput failed");
            System.Threading.Thread.Sleep(80);
        }
    }
    public static void Key(int vk, bool up) {
        uint scan=MapVirtualKey((uint)vk,4);
        INPUT i=new INPUT(); i.type=1; i.data.key.vk=(ushort)vk; i.data.key.scan=(ushort)(scan & 255);
        i.data.key.flags=(scan!=0 ? 8u:0u) | (up?2u:0u) | ((scan & 0xff00)==0xe000 ? 1u:0u);
        if(SendInput(1,new INPUT[]{i},Marshal.SizeOf(typeof(INPUT)))!=1) throw new Exception("SendInput failed");
    }
}
'@

$named = @{ ESC=0x1B; ENTER=0x0D; SPACE=0x20; TAB=0x09; UP=0x26; DOWN=0x28; LEFT=0x25; RIGHT=0x27;
    F1=0x70; F2=0x71; F3=0x72; F4=0x73; F5=0x74; F6=0x75; F7=0x76; F8=0x77; F9=0x78; F10=0x79; F11=0x7A; F12=0x7B;
    Q=0x51; W=0x57; A=0x41; S=0x53; D=0x44; E=0x45 }

function Get-Game { Get-Process SbzEngine -ErrorAction SilentlyContinue | Select-Object -First 1 }
function Focus-Game {
    $game = Get-Game
    if (-not $game) { throw 'Game is not running.' }
    $handle = $game.MainWindowHandle
    if ($handle -eq [IntPtr]::Zero) { throw 'Game has no main window yet.' }
    if ([DriverInput]::GetForegroundWindow() -ne $handle) {
        [void][DriverInput]::AllowSetForegroundWindow($game.Id)
        # An Alt tap lets this process move the foreground (Windows foreground lock).
        [DriverInput]::Key(0x12,$false); [DriverInput]::Key(0x12,$true)
        [void][DriverInput]::ShowWindow($handle,9)
        [void][DriverInput]::SetForegroundWindow($handle)
        Start-Sleep -Milliseconds 500
    }
    if ([DriverInput]::GetForegroundWindow() -ne $handle) { throw 'Game is not foreground; no input sent.' }
    $handle
}
function Press([int]$Vk, [int]$Hold = 100) {
    $handle = (Get-Game).MainWindowHandle
    if ([DriverInput]::GetForegroundWindow() -ne $handle) { throw 'Game lost focus; stopped sending input.' }
    [DriverInput]::Key($Vk,$false)
    try { Start-Sleep -Milliseconds $Hold } finally { [DriverInput]::Key($Vk,$true) }
    Start-Sleep -Milliseconds 80
}
function With-Modifier([int]$Mod, [int]$Vk) {
    [DriverInput]::Key($Mod,$false); try { Press $Vk } finally { [DriverInput]::Key($Mod,$true) }
}
function Toggle-Console {
    [DriverInput]::Key(0x11,$false)
    try { [DriverInput]::Key(0x12,$false); try { Press 0xC0 } finally { [DriverInput]::Key(0x12,$true) } }
    finally { [DriverInput]::Key(0x11,$true) }
    Start-Sleep -Milliseconds 400
}
function Type-Text([string]$Value) {
    foreach ($char in $Value.ToCharArray()) {
        switch -CaseSensitive ($char) {
            '"' { With-Modifier 0x10 0xDE }
            '_' { With-Modifier 0x10 0xBD }
            '-' { Press 0xBD }
            '.' { Press 0xBE }
            '=' { Press 0xBB }
            '\' { Press 0xDC }
            default { Press ([int][char]::ToUpperInvariant($char)) }
        }
    }
}
function Log-Length { if (Test-Path -LiteralPath $log) { (Get-Item -LiteralPath $log).Length } else { 0 } }
function Read-LogFrom([long]$Offset) {
    if (-not (Test-Path -LiteralPath $log)) { return '' }
    $stream = [IO.File]::Open($log,'Open','Read','ReadWrite,Delete')
    try {
        if ($Offset -gt $stream.Length) { $Offset = 0 }
        [void]$stream.Seek($Offset,'Begin')
        (New-Object IO.StreamReader($stream)).ReadToEnd()
    } finally { $stream.Dispose() }
}

function Get-LaunchArguments([string[]]$ExtraArgs) {
    # Start-Process joins ArgumentList with spaces; build one native command line
    # with Windows quoting, including empty tokens and trailing backslashes.
    ($ExtraArgs | ForEach-Object {
        $escaped = [regex]::Replace($_, '(\\*)"', '$1$1\"')
        $escaped = [regex]::Replace($escaped, '(\\+)$', '$1$1')
        '"' + $escaped + '"'
    }) -join ' '
}
function Get-QuickMapEnvironment([string]$Content, [string]$Map) {
    if ($Map -notmatch '^[A-Za-z0-9_-]+$') { throw 'Map must be a bare map name, e.g. Pa1_Intro (no extension).' }
    # QUICKMAP is evaluated by GameWorld frontend init after its resources exist.
    # -map dispatches from the earlier EXE startup parser instead.
    $cfg = $Content -replace '(?im)^\s*QUICKMAP\s*=.*(?:\r?\n|$)', ''
    $cfg.TrimEnd() + "`r`nQUICKMAP=$Map`r`n"
}
function Wait-DriverFirstFrame([long]$Offset, [int]$TimeoutSec) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        if ((Read-LogFrom $Offset) -match 'First frame presented: [1-9][0-9]*x[1-9][0-9]*') { return }
        if (-not (Get-Game)) { throw 'Game exited before its first frame.' }
        if ((Get-Date) -ge $deadline) { throw "No first frame within $TimeoutSec s; see BigFix.log." }
        Start-Sleep -Milliseconds 100
    } while ($true)
}
function Get-DriverCaptureSettings([string]$Content) {
    $settings = @{ Count=1; Key='F12'; Png=$true }
    $entries = @()
    $section = ''; $lineNumber = 0
    foreach ($line in (($Content -replace '^\uFEFF','') -split '\r?\n')) {
        $lineNumber++
        $line = ($line -split ';',2)[0].Trim()
        if ($line -match '^\[(.*)\]$') { $section=$Matches[1].Trim().ToLowerInvariant(); continue }
        if ($section -notin @('','general','bigfix')) { continue }
        if ($line -match '^(CaptureBurst|CaptureKey|CapturePng)\s*=\s*(.*?)\s*$') {
            $entries += [pscustomobject]@{ Key=$Matches[1]; Value=$Matches[2]; Line=$lineNumber;
                Priority=$(if ($section -eq 'bigfix') {2} else {1}) }
        }
    }
    # Same priority/last-valid-value rules as parseSettings in src/config.cpp.
    foreach ($entry in ($entries | Sort-Object Priority,Line)) {
        $value=$entry.Value
        switch ($entry.Key) {
            'CaptureBurst' {
                if ($value -cmatch '^-?[0-9]+$') {
                    $count=0
                    if ([int]::TryParse($value,[ref]$count) -and $count -ge 1 -and $count -le 16) { $settings.Count=$count }
                }
            }
            'CaptureKey' {
                if ($value -match '^F([0-9]+)$') {
                    $key=0
                    if ([int]::TryParse($Matches[1],[ref]$key) -and $key -ge 1 -and $key -le 24) { $settings.Key="F$key" }
                }
            }
            'CapturePng' { if ($value -in @('0','1')) { $settings.Png=$value -eq '1' } }
        }
    }
    [pscustomobject]$settings
}
function Wait-DriverBurst([long]$Offset, [int]$Count, [int]$TimeoutSec) {
    $deadline = (Get-Date).AddSeconds($TimeoutSec)
    do {
        $new = Read-LogFrom $Offset
        if ($new -match 'Capture failed[^\r\n]*') { throw $Matches[0] }
        $paths = @([regex]::Matches($new, '(?m)Capture saved: (.+\.png) [0-9]+x[0-9]+\r?$') |
            ForEach-Object { $_.Groups[1].Value } | Select-Object -Unique)
        if ($paths.Count -ge $Count) {
            $paths = @($paths | Select-Object -First $Count)
            if (@($paths | Where-Object { -not (Test-Path -LiteralPath $_ -PathType Leaf) }).Count -eq 0) { return $paths }
        }
        if (-not (Get-Game)) { throw 'Game exited before the capture burst completed.' }
        if ((Get-Date) -ge $deadline) { throw "Only $($paths.Count)/$Count burst PNGs within $TimeoutSec s; see BigFix.log." }
        Start-Sleep -Milliseconds 100
    } while ($true)
}

switch ($Action) {
    'launch' {
        if (Get-Game) { throw 'Game already running; quit first.' }
        if (Test-Path -LiteralPath $backupDir) { throw "Backups from an unfinished run exist in $backupDir; run quit first." }
        $arguments = Get-LaunchArguments -ExtraArgs $ExtraArgs
        $mapEnvironment = if ($Map) { Get-QuickMapEnvironment -Content ([IO.File]::ReadAllText($environment)) -Map $Map }
        New-Item -ItemType Directory -Path $backupDir | Out-Null
        foreach ($path in @($ini,$environment,$asi)) {
            if (Test-Path -LiteralPath $path) { Copy-Item -LiteralPath $path -Destination (Join-Path $backupDir (Split-Path $path -Leaf)) }
        }
        if ($PluginPath) { Copy-Item -LiteralPath $PluginPath -Destination $asi -Force }
        $lines = [Collections.Generic.List[string]]::new()
        $lines.Add('[BigFix]')
        $base = if (Test-Path -LiteralPath (Join-Path $backupDir 'BigFix.ini')) { Get-Content -LiteralPath (Join-Path $backupDir 'BigFix.ini') } else { @() }
        $overrides = [ordered]@{}
        foreach ($pair in ($Settings -split ';' | Where-Object { $_ -match '=' })) {
            $key,$value = $pair -split '=',2; $overrides[$key.Trim()] = $value.Trim()
        }
        foreach ($line in $base) {
            if ($line -match '^\s*\[') { continue }
            if ($line -match '^\s*([^=;#]+?)\s*=' -and $overrides.Contains($Matches[1])) { continue }
            $lines.Add($line)
        }
        foreach ($key in $overrides.Keys) { $lines.Add("$key=$($overrides[$key])") }
        [IO.File]::WriteAllLines($ini, $lines, [Text.Encoding]::ASCII)
        if ($Map) { [IO.File]::WriteAllText($environment, $mapEnvironment, [Text.Encoding]::ASCII) }
        if ($ShaderMode) {
            $cfg = (Get-Content -LiteralPath $environment -Raw) -replace '(?im)^\s*XR_SHADERMODE\s*=.*(?:\r?\n|$)',''
            [IO.File]::WriteAllText($environment, $cfg.TrimEnd() + "`r`nXR_SHADERMODE=$ShaderMode`r`n", [Text.Encoding]::ASCII)
        }
        # Rotate before launch, so an old FirstFrame cannot satisfy the wait.
        # This is the same rotation BigFix would do when opening its log.
        if (Test-Path -LiteralPath $log) {
            Move-Item -LiteralPath $log -Destination (Join-Path $systemDir 'BigFix.prev.log') -Force
        }
        $start = @{ FilePath=$exe; WorkingDirectory=$systemDir; PassThru=$true }
        if ($arguments) { $start.ArgumentList=$arguments }
        $game = Start-Process @start
        if ($WaitFirstFrame) {
            Wait-DriverFirstFrame -Offset 0 -TimeoutSec $TimeoutSec
            try { [void](Focus-Game) } catch { Write-Host "focus: $($_.Exception.Message)" }
        }
        "launched pid=$($game.Id); settings: $($overrides.Keys -join ',')"
    }
    'key' {
        [void](Focus-Game)
        foreach ($name in ($Keys -split ',')) {
            $k = $name.Trim().ToUpperInvariant()
            $vk = if ($named.ContainsKey($k)) { $named[$k] } elseif ($k.Length -eq 1) { [int][char]$k } else { throw "Unknown key $k" }
            Press $vk $HoldMs
            Start-Sleep -Milliseconds 300
        }
        "pressed $Keys"
    }
    'console' {
        [void](Focus-Game)
        Toggle-Console
        Type-Text $Text
        Press 0x0D
        Toggle-Console
        "console: $Text"
    }
    'shot' {
        [void](Focus-Game)
        $offset = Log-Length
        Press 0x7B 1000  # held past the game's own TGA write, which stalls a frame
        $deadline = (Get-Date).AddSeconds(15)
        do {
            Start-Sleep -Milliseconds 300
            $new = Read-LogFrom $offset
            if ($new -match 'Capture failed[^\r\n]*') { throw $Matches[0] }
        } while ((Get-Date) -lt $deadline -and -not ($new -match 'Capture saved: (.+?\.png)'))
        if (-not ($new -match 'Capture saved: (.+?\.png)')) { throw 'No capture within 15 s; see BigFix.log.' }
        $destination = Join-Path $outRoot ("{0}-{1}.png" -f (Get-Date -Format HHmmss), $Name)
        Start-Sleep -Milliseconds 300
        Copy-Item -LiteralPath $Matches[1].Trim() -Destination $destination
        # A 960-px JPEG preview keeps step-by-step viewing cheap; the PNG stays full size.
        Add-Type -AssemblyName System.Drawing
        $full = [Drawing.Image]::FromFile($destination)
        try {
            $preview = New-Object Drawing.Bitmap 960, ([int](960 * $full.Height / $full.Width))
            $g = [Drawing.Graphics]::FromImage($preview)
            $g.InterpolationMode = 'HighQualityBicubic'
            $g.DrawImage($full, 0, 0, $preview.Width, $preview.Height)
            $g.Dispose()
            $preview.Save([IO.Path]::ChangeExtension($destination, '.preview.jpg'), [Drawing.Imaging.ImageFormat]::Jpeg)
            $preview.Dispose()
        } finally { $full.Dispose() }
        [IO.Path]::ChangeExtension($destination, '.preview.jpg')
    }
    'burst' {
        $capture = Get-DriverCaptureSettings -Content $(if (Test-Path -LiteralPath $ini) { [IO.File]::ReadAllText($ini) } else { '' })
        if (-not $capture.Png) { throw 'burst requires CapturePng=1 in BigFix.ini.' }
        if ($capture.Key -ne 'F12') { throw 'burst requires CaptureKey=F12 in BigFix.ini.' }
        [void](Focus-Game)
        $offset = Log-Length
        Press 0x7B 1000 # one down/up edge; hold across the stock TGA stall
        Wait-DriverBurst -Offset $offset -Count $capture.Count -TimeoutSec $TimeoutSec
    }
    'wait-log' {
        $offset = Log-Length
        $deadline = (Get-Date).AddSeconds($TimeoutSec)
        do {
            Start-Sleep -Milliseconds 500
            $new = Read-LogFrom $offset
            $hit = [regex]::Match($new, "[^\r\n]*(?:$Pattern)[^\r\n]*")
        } while (-not $hit.Success -and (Get-Date) -lt $deadline -and (Get-Game))
        if (-not $hit.Success) { throw "No log line matching '$Pattern' within $TimeoutSec s." }
        $hit.Value
    }
    'click' {
        [void](Focus-Game)
        [DriverInput]::Click()
        'clicked'
    }
    'look' {
        [void](Focus-Game)
        [DriverInput]::Move($Dx, $Dy, [math]::Max(1,$Steps))
        "looked dx=$Dx dy=$Dy"
    }
    'status' {
        $game = Get-Game
        if (-not $game) { 'not running' } else {
            "pid=$($game.Id) foreground=$([DriverInput]::GetForegroundWindow() -eq $game.MainWindowHandle) cpu=$([math]::Round($game.CPU,1))s"
        }
        if (Test-Path -LiteralPath $log) { Get-Content -LiteralPath $log -Tail 8 }
    }
    'quit' {
        $game = Get-Game
        if ($game) {
            [void]$game.CloseMainWindow()
            if (-not $game.WaitForExit(15000)) { Stop-Process -Id $game.Id; $game.WaitForExit() }
        }
        if (Test-Path -LiteralPath $log) { Copy-Item -LiteralPath $log -Destination (Join-Path $outRoot ("BigFix-{0}.log" -f (Get-Date -Format HHmmss))) }
        if (Test-Path -LiteralPath $backupDir) {
            foreach ($path in @($ini,$environment,$asi)) {
                $saved = Join-Path $backupDir (Split-Path $path -Leaf)
                if (Test-Path -LiteralPath $saved) { Copy-Item -LiteralPath $saved -Destination $path -Force }
            }
            Remove-Item -LiteralPath $backupDir -Recurse -Force
        }
        'quit; settings restored'
    }
}

<#
.SYNOPSIS
    Launch the game once with BigFix installed, check the log, take one capture.

.DESCRIPTION
    A sparing, scripted milestone smoke run (plan Task 12). It backs up
    Environment.cfg, installs the plugin from a staging folder with
    installer\install.ps1, launches System\Win64_AMD64\SbzEngine.exe in the
    foreground, waits for the first presented frame, lets the intro play for
    20 seconds, sends ESC once, waits five seconds for the menu, confirms
    foreground again, presses the capture key once, then closes the game.

    Checks: plugin_loaded, build_EU11, texture_patch, modes_filtered,
    borderless, first_frame_native, capture_png, desktop_unchanged, and
    capture_foreground when a frame is ready. Exit code 0 only if all passed.

    Log wording is matched through $Patterns below; adjust it there when the
    plugin's messages change.

.PARAMETER GameDir
    The game folder (the one containing System\Win64_AMD64\SbzEngine.exe).

.PARAMETER Seconds
    FirstFrame readiness timeout, not total runtime. Default: 40. Use -Seconds 60
    for a short video/capture check: the 190-second intro is cut short by one
    ESC 20 seconds after FirstFrame; capture follows in the menu after 5 seconds.

.PARAMETER TraceVideo
    Write TraceVideo=1 into the installed BigFix.ini before launch. Optional,
    bounded read-only video diagnostics in BigFix.log; absent from shipped INI.

.PARAMETER AntiAliasing
    Write AntiAliasing into the installed BigFix.ini (0, 2, 4 or 8 samples).
    Omit to keep the installed setting.

.PARAMETER TracePixelFormat
    Write TracePixelFormat=1 into the installed BigFix.ini for bounded AA diagnostics.

.PARAMETER TraceInput
    Write TraceInput=1 into BigFix.ini before launch. Sampled mouse poll
    HRESULT/count/overflow/raw deltas and cumulative totals (at most 1500 lines).

.PARAMETER MouseBurst
    After the menu capture, send 2000 relative mouse moves about 1 ms apart:
    a circle followed by the exact reverse circle. Log attempted/sent counts.
    Combine with -TraceInput to compare raw-on/off motion and DI overflow counts.

.PARAMETER Restore
    Put the backed-up Environment.cfg back and uninstall the plugin at the end.

.PARAMETER PluginDir
    Folder holding the built BigFix.asi (and optionally BigFix.ini).
    Default: build\Release.

.PARAMETER NoInstall
    Skip install.ps1: run whatever plugin is already in the game folder.

.PARAMETER LoaderDll
    Ultimate ASI Loader DLL to install as winmm.dll. Default: winmm.dll in
    PluginDir, then the repo root, then dist\.

.PARAMETER CaptureKeyVk
    Virtual-key code of the capture key. Default: 0x7B (F12).

.PARAMETER Patterns
    Hashtable overriding the log patterns in $defaultPatterns.

.EXAMPLE
    pwsh tools\smoke_run.ps1 -GameDir D:\Games\Riddick -Restore
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$GameDir,

    [int]$Seconds = 40,

    [switch]$TraceVideo,

    [ValidateSet(0, 2, 4, 8)]
    [int]$AntiAliasing,

    [switch]$TracePixelFormat,

    [switch]$TraceInput,

    [switch]$MouseBurst,

    [switch]$Restore,

    [string]$PluginDir,

    [switch]$NoInstall,

    [string]$LoaderDll,

    [int]$CaptureKeyVk = 0x7B,

    [hashtable]$Patterns = @{}
)

$ErrorActionPreference = 'Stop'

# Log-line patterns. The plugin's exact wording may change; the lead aligns
# these rather than the script's logic.
$defaultPatterns = @{
    VersionHeader  = 'BigFix \d+\.\d+\.\d+'           # log exists and has the version header
    Build          = 'RndrGL\.dll sha256=[0-9a-f]{64} build=EU11_x64\b'
    TexturePatch   = 'RndrGL\.dll texture_size_clamp:'
    TextureApplied = 'RndrGL\.dll texture_size_clamp: (?:Applied|AlreadyApplied)\b'
    ModesFiltered  = 'Display modes: \d+ -> (\d+) \(cap=512\)' # first group: how many modes survived
    BorderlessSelection = 'Borderless render selection: [^\r\n]* -> (\d+)x(\d+)(?=\s|$)'
    Borderless     = 'Borderless window style:'
    FirstFrame     = 'First frame presented: (\d+)x(\d+)'
}
foreach ($key in $Patterns.Keys) { $defaultPatterns[$key] = $Patterns[$key] }
$Patterns = $defaultPatterns

# --- helpers ---------------------------------------------------------------

$script:Checks = New-Object System.Collections.Generic.List[object]
$script:Failed = 0

function Add-Check {
    param([string]$Name, [bool]$Ok, [string]$Detail)
    $status = 'FAIL'
    if ($Ok) { $status = 'PASS' } else { $script:Failed++ }
    $script:Checks.Add([pscustomobject]@{ Name = $Name; Ok = $Ok; Detail = $Detail })
    $line = '{0} {1}' -f $status, $Name
    if ($Detail) { $line = $line + '  ' + $Detail }
    Write-Host $line
}

function Read-LogText {
    param([string]$Path)
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return '' }
    $stream = $null
    $reader = $null
    try {
        # ReadAllText uses FileShare.Read, which conflicts with the game's
        # still-open ofstream writer. After exit it worked, hiding this failure
        # in first_frame_native while capture readiness always timed out.
        $stream = [System.IO.File]::Open($Path, [System.IO.FileMode]::Open,
            [System.IO.FileAccess]::Read, [System.IO.FileShare]::ReadWrite)
        $reader = New-Object System.IO.StreamReader($stream)
        return $reader.ReadToEnd()
    } catch {
        return ''
    } finally {
        if ($reader) { $reader.Dispose() }
        elseif ($stream) { $stream.Dispose() }
    }
}

function Wait-FirstFrame {
    param($Process, [string]$LogPath, [string]$Pattern, [int]$Seconds)
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        $Process.Refresh()
        if ((Read-LogText -Path $LogPath) -match $Pattern) { return $true }
        if ($Process.HasExited) { break }
        Start-Sleep -Milliseconds 500
    }
    return $false
}

function Enable-SmokeTrace {
    param([string]$Path, [ValidateSet('TraceVideo', 'TraceInput')][string]$Key)
    $text = ''
    if (Test-Path -LiteralPath $Path -PathType Leaf) { $text = [System.IO.File]::ReadAllText($Path) }
    # Update all recognized occurrences so duplicate keys cannot keep it off.
    # A key in an unrelated section does not count as a BigFix setting.
    $section = ''; $found = $false
    $lines = @([regex]::Split($text, '\r?\n') | ForEach-Object {
        $line = $_
        if ($line -match '^\s*\[([^\]]+)\]\s*(?:;.*)?$') { $section = $Matches[1].Trim().ToLowerInvariant() }
        if (($section -eq '' -or $section -eq 'bigfix' -or $section -eq 'general') -and $line -match ('^\s*' + $Key + '\s*=')) {
            $line = $Key + '=1'; $found = $true
        }
        $line
    })
    if (-not $found) { $lines += @('[BigFix]', ($Key + '=1'), '') }
    $text = $lines -join "`r`n"
    [System.IO.File]::WriteAllText($Path, $text, (New-Object System.Text.UTF8Encoding($false)))
    Write-Host ($Key + '=1 enabled in ' + $Path)
}

function Enable-VideoTrace {
    param([string]$Path)
    Enable-SmokeTrace -Path $Path -Key TraceVideo
}

function Enable-InputTrace {
    param([string]$Path)
    Enable-SmokeTrace -Path $Path -Key TraceInput
}

function Set-MsaaOptions {
    param([string]$Path, [ValidateSet(0, 2, 4, 8)][int]$AntiAliasing, [switch]$TracePixelFormat)
    $values = @{}
    if ($PSBoundParameters.ContainsKey('AntiAliasing')) { $values['AntiAliasing'] = [string]$AntiAliasing }
    if ($TracePixelFormat) { $values['TracePixelFormat'] = '1' }
    $text = ''
    if (Test-Path -LiteralPath $Path -PathType Leaf) { $text = [System.IO.File]::ReadAllText($Path) }
    $section = ''; $found = @{}
    $lines = @([regex]::Split($text, '\r?\n') | ForEach-Object {
        $line = $_
        if ($line -match '^\s*\[([^\]]+)\]\s*(?:;.*)?$') { $section = $Matches[1].Trim().ToLowerInvariant() }
        if (($section -eq '' -or $section -eq 'bigfix' -or $section -eq 'general') -and $line -match '^\s*([^=]+?)\s*=') {
            $key = $Matches[1].Trim()
            if ($values.ContainsKey($key)) { $line = $key + '=' + $values[$key]; $found[$key] = $true }
        }
        $line
    })
    $missing = @($values.Keys | Where-Object { -not $found.ContainsKey($_) } | Sort-Object)
    if ($missing.Count) {
        $lines += '[BigFix]'
        foreach ($key in $missing) { $lines += $key + '=' + $values[$key] }
        $lines += ''
    }
    [System.IO.File]::WriteAllText($Path, ($lines -join "`r`n"), (New-Object System.Text.UTF8Encoding($false)))
}

function Wait-IntroThenMenu {
    param($Process)
    Write-Host 'FirstFrame observed; letting videos play for 20 s before one ESC.'
    for ($i = 0; $i -lt 40; $i++) {
        $Process.Refresh()
        if ($Process.HasExited) { return $false }
        Start-Sleep -Milliseconds 500
    }
    if (-not (Set-GameForeground -Process $Process)) {
        Write-Host 'ESC not sent: game window is not foreground.'
        return $false
    }
    $sent = Send-CaptureKey -VirtualKey 0x1B
    Write-Host ('{0:o} ESC SendInput sent={1}; 20 s after FirstFrame (plus focus time).' -f (Get-Date), $sent)
    if (-not $sent) { return $false }
    # Give the one skip transition time to present the menu; no repeated ESCs.
    for ($i = 0; $i -lt 10; $i++) {
        $Process.Refresh()
        if ($Process.HasExited) { return $false }
        Start-Sleep -Milliseconds 500
    }
    return $true
}

function Test-FirstFrameNative {
    param([string]$LogText, [string]$Pattern, [string]$DesktopMode)
    $frame = [regex]::Match($LogText, $Pattern)
    if (-not $frame.Success -or $DesktopMode -notmatch '^(\d+)x(\d+)@') { return $false }
    return [int]$frame.Groups[1].Value -gt 0 -and [int]$frame.Groups[2].Value -gt 0 -and
        [int]$frame.Groups[1].Value -eq [int]$Matches[1] -and [int]$frame.Groups[2].Value -eq [int]$Matches[2]
}

function Get-ModesFilteredCheck {
    param([string]$LogText, [hashtable]$Patterns, [string]$DesktopMode)
    $count = [regex]::Match($LogText, $Patterns.ModesFiltered)
    $detail = 'no filtered count or native borderless render selection in the log'
    if ($count.Success) {
        $modes = [int]$count.Groups[1].Value
        $detail = ('Display modes: {0} modes (limit 512)' -f $modes)
        if ($modes -le 512) { return [pscustomobject]@{ Ok = $true; Detail = $detail } }
    }
    # Borderless forces the desktop mode at the MSystem selection boundary;
    # the renderer need not enumerate modes through EnumDisplaySettings.
    if ($DesktopMode -match '^(\d+)x(\d+)@') {
        $width = [int]$Matches[1]; $height = [int]$Matches[2]
        foreach ($selection in [regex]::Matches($LogText, $Patterns.BorderlessSelection)) {
            if ($width -gt 0 -and $height -gt 0 -and
                [int]$selection.Groups[1].Value -eq $width -and [int]$selection.Groups[2].Value -eq $height) {
                return [pscustomobject]@{ Ok = $true; Detail = $selection.Value }
            }
        }
    }
    return [pscustomobject]@{ Ok = $false; Detail = $detail }
}

function Get-CaptureFailureDetail {
    param([string]$LogText, [string]$DefaultDetail)
    $detail = $DefaultDetail
    if ($LogText -match 'Capture key down: foreground=0\b') { $detail += '; Capture key down: foreground=0' }
    $failure = [regex]::Matches($LogText, 'Capture failed: [^\r\n]+')
    if ($failure.Count -gt 0) { $detail += '; ' + $failure[$failure.Count - 1].Value }
    return $detail
}

function Wait-NewCapture {
    param([string]$ShotDir, [string[]]$ExistingShots, [int]$Seconds = 10)
    $deadline = (Get-Date).AddSeconds($Seconds)
    while ((Get-Date) -lt $deadline) {
        if (Test-Path -LiteralPath $ShotDir -PathType Container) {
            $newShots = @(Get-ChildItem -LiteralPath $ShotDir -File -Filter 'BigFix_*.png' |
                Where-Object { $ExistingShots -notcontains $_.Name })
            if ($newShots.Count -gt 0) { return $true }
        }
        Start-Sleep -Milliseconds 100
    }
    return $false
}

function Get-DesktopMode {
    $controller = Get-CimInstance -ClassName Win32_VideoController -ErrorAction Stop |
        Where-Object { $_.CurrentHorizontalResolution -gt 0 -and $_.CurrentVerticalResolution -gt 0 } |
        Sort-Object -Property @{ Expression = { [int64]$_.CurrentHorizontalResolution * [int64]$_.CurrentVerticalResolution } } -Descending |
        Select-Object -First 1
    if (-not $controller) { return $null }
    return ('{0}x{1}@{2}' -f $controller.CurrentHorizontalResolution,
        $controller.CurrentVerticalResolution, $controller.CurrentRefreshRate)
}

function Get-PngSize {
    param([string]$Path)
    try {
        $bytes = [System.IO.File]::ReadAllBytes($Path)
    } catch {
        return $null
    }
    if ($bytes.Length -lt 24) { return $null }
    if ($bytes[0] -ne 0x89 -or $bytes[1] -ne 0x50 -or $bytes[2] -ne 0x4E -or $bytes[3] -ne 0x47) { return $null }
    if ([System.Text.Encoding]::ASCII.GetString($bytes, 12, 4) -ne 'IHDR') { return $null }
    $width = ([int]$bytes[16] -shl 24) -bor ([int]$bytes[17] -shl 16) -bor ([int]$bytes[18] -shl 8) -bor [int]$bytes[19]
    $height = ([int]$bytes[20] -shl 24) -bor ([int]$bytes[21] -shl 16) -bor ([int]$bytes[22] -shl 8) -bor [int]$bytes[23]
    return [pscustomobject]@{ Width = $width; Height = $height }
}

function Resolve-LoaderDll {
    param([string]$Path, [string]$BuildPath, [string]$RepoRoot)
    $candidates = New-Object System.Collections.Generic.List[string]
    if ($Path) {
        if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Loader DLL not found: $Path" }
        return (Resolve-Path -LiteralPath $Path).Path
    }
    if ($BuildPath) { $candidates.Add((Join-Path $BuildPath 'winmm.dll')) }
    $candidates.Add((Join-Path $RepoRoot 'winmm.dll'))
    $candidates.Add((Join-Path $RepoRoot 'dist\winmm.dll'))
    foreach ($candidate in $candidates) {
        if (Test-Path -LiteralPath $candidate -PathType Leaf) { return (Resolve-Path -LiteralPath $candidate).Path }
    }
    throw ("winmm.dll (Ultimate ASI Loader) not found. Tried: " + ($candidates -join ', ') + ". Pass -LoaderDll <path>.")
}

function New-StagingDir {
    param([string]$BuildPath, [string]$LoaderDllPath, [string]$DefaultIni)
    $staging = Join-Path ([System.IO.Path]::GetTempPath()) ('bigfix-smoke-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $staging | Out-Null
    $asi = Join-Path $BuildPath 'BigFix.asi'
    if (-not (Test-Path -LiteralPath $asi -PathType Leaf)) {
        $asi = Get-ChildItem -LiteralPath $BuildPath -Filter '*.asi' |
            Sort-Object -Property Name | Select-Object -First 1 -ExpandProperty FullName
    }
    if (-not $asi) { throw "No BigFix.asi in $BuildPath" }
    Copy-Item -LiteralPath $asi -Destination (Join-Path $staging 'BigFix.asi')
    # A build need not contain an INI, but the installer requires one.
    $ini = Join-Path $BuildPath 'BigFix.ini'
    if (Test-Path -LiteralPath $ini -PathType Leaf) {
        Copy-Item -LiteralPath $ini -Destination (Join-Path $staging 'BigFix.ini')
    } else {
        Copy-Item -LiteralPath $DefaultIni -Destination (Join-Path $staging 'BigFix.ini')
    }
    Copy-Item -LiteralPath $LoaderDllPath -Destination (Join-Path $staging 'winmm.dll')
    return $staging
}

function Initialize-GameInput {
    $source = @'
using System;
using System.Runtime.InteropServices;
public static class BigFixFocus {
    [DllImport("user32.dll")] public static extern bool AllowSetForegroundWindow(int dwProcessId);
    [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int nCmdShow);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
    [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
    [DllImport("user32.dll")] private static extern uint MapVirtualKey(uint code, uint mapType);
    [DllImport("user32.dll", SetLastError = true)] private static extern uint SendInput(uint count, INPUT[] inputs, int size);
    [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT {
        public ushort wVk, wScan;
        public uint dwFlags, time;
        public UIntPtr dwExtraInfo;
    }
    [StructLayout(LayoutKind.Sequential)] public struct MOUSEINPUT {
        public int dx, dy;
        public uint mouseData, dwFlags, time;
        public UIntPtr dwExtraInfo;
    }
    // INPUT's union must include the larger mouse member even for keyboard
    // events, so SendInput gets the native 40-byte (x64) / 28-byte (x86) size.
    [StructLayout(LayoutKind.Explicit)] public struct INPUTDATA {
        [FieldOffset(0)] public KEYBDINPUT keyboard;
        [FieldOffset(0)] public MOUSEINPUT mouse;
    }
    [StructLayout(LayoutKind.Sequential)] public struct INPUT {
        public uint type;
        public INPUTDATA data;
    }
    public static INPUT KeyInput(int vk, bool up) {
        uint scan = MapVirtualKey((uint)vk, 4); // MAPVK_VK_TO_VSC_EX
        INPUT input = new INPUT();
        input.type = 1; // INPUT_KEYBOARD
        input.data.keyboard.wVk = (ushort)vk;
        input.data.keyboard.wScan = (ushort)(scan & 0xff);
        input.data.keyboard.dwFlags = (scan != 0 ? 8u : 0u) | (up ? 2u : 0u) |
            ((scan & 0xff00) == 0xe000 ? 1u : 0u); // SCANCODE, KEYUP, EXTENDEDKEY
        return input;
    }
    public static bool SendKey(int vk, bool up) {
        INPUT input = KeyInput(vk, up);
        return SendInput(1, new INPUT[] { input }, Marshal.SizeOf(typeof(INPUT))) == 1;
    }
    public static INPUT[] MouseBurstInputs() {
        INPUT[] moves = new INPUT[2000];
        // Differences of integer positions close the first circle exactly.
        // Reverse the same increments so rounding cannot introduce drift.
        const double radius = 600.0;
        int previousX = (int)radius, previousY = 0;
        for (int i = 0; i < 1000; ++i) {
            double angle = 2.0 * Math.PI * (i + 1) / 1000.0;
            int x = (int)Math.Round(radius * Math.Cos(angle));
            int y = (int)Math.Round(radius * Math.Sin(angle));
            INPUT move = new INPUT();
            move.data.mouse.dx = x - previousX; move.data.mouse.dy = y - previousY;
            move.data.mouse.dwFlags = 0x2001; // MOVE | MOVE_NOCOALESCE; never ABSOLUTE
            moves[i] = move;
            move.data.mouse.dx = -move.data.mouse.dx; move.data.mouse.dy = -move.data.mouse.dy;
            moves[1999-i] = move;
            previousX = x; previousY = y;
        }
        return moves;
    }
    public static int[] SendMouseBurst(IntPtr window) {
        INPUT[] moves = MouseBurstInputs();
        INPUT[] one = new INPUT[1];
        int attempted = 0, sent = 0;
        // Request millisecond timer resolution only for this bounded burst.
        bool timer = timeBeginPeriod(1) == 0;
        try {
            foreach (INPUT move in moves) {
                if (GetForegroundWindow() != window) break;
                one[0] = move; ++attempted;
                sent += (int)SendInput(1, one, Marshal.SizeOf(typeof(INPUT)));
                System.Threading.Thread.Sleep(1);
            }
        } finally { if (timer) timeEndPeriod(1); }
        return new int[] { attempted, sent };
    }
    [DllImport("winmm.dll")] private static extern uint timeBeginPeriod(uint period);
    [DllImport("winmm.dll")] private static extern uint timeEndPeriod(uint period);
}
'@
    if (-not ('BigFixFocus' -as [type])) { Add-Type -TypeDefinition $source }
}

function Set-GameForeground {
    param($Process)
    Initialize-GameInput
    $handle = [IntPtr]::Zero
    $deadline = (Get-Date).AddSeconds(20)
    while ((Get-Date) -lt $deadline) {
        $Process.Refresh()
        if ($Process.HasExited) { return $false }
        $handle = $Process.MainWindowHandle
        if ($handle -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 250
    }
    if ($handle -eq [IntPtr]::Zero) { return $false }
    [void][BigFixFocus]::AllowSetForegroundWindow($Process.Id)
    # A synthetic Alt tap makes the next SetForegroundWindow stick.
    [void][BigFixFocus]::SendKey(0x12, $false)
    Start-Sleep -Milliseconds 50
    [void][BigFixFocus]::SendKey(0x12, $true)
    [void][BigFixFocus]::ShowWindow($handle, 9)   # SW_RESTORE
    [void][BigFixFocus]::SetForegroundWindow($handle)
    return [BigFixFocus]::GetForegroundWindow() -eq $handle
}

function Send-CaptureKey {
    param([int]$VirtualKey)
    Initialize-GameInput
    if (-not [BigFixFocus]::SendKey($VirtualKey, $false)) { return $false }
    try { Start-Sleep -Milliseconds 80 }
    finally { $released = [BigFixFocus]::SendKey($VirtualKey, $true) }
    return $released
}

function Send-MouseBurst {
    param($Process)
    Initialize-GameInput
    $Process.Refresh()
    if ($Process.HasExited -or -not (Set-GameForeground -Process $Process)) {
        Write-Host 'MouseBurst attempted=0 sent=0 expected=2000: game is not foreground'
        return $false
    }
    $counts = [BigFixFocus]::SendMouseBurst($Process.MainWindowHandle)
    Write-Host ('{0:o} MouseBurst attempted={1} sent={2} expected=2000 (circle then reverse)' -f (Get-Date), $counts[0], $counts[1])
    # Let the game consume the tail, and the sampled trace report its totals.
    Start-Sleep -Milliseconds 250
    return $counts[0] -eq 2000 -and $counts[1] -eq 2000
}

function Stop-GameProcess {
    param([System.Diagnostics.Process]$Process)
    try {
        [void]$Process.CloseMainWindow()
        [void]$Process.WaitForExit(8000)
    } catch { }
    if (-not $Process.HasExited) {
        Write-Host 'Game did not close on request; stopping it.'
        try { Stop-Process -Id $Process.Id -Force -ErrorAction Stop } catch { }
        try { [void]$Process.WaitForExit(8000) } catch { }
    }
}

# --- setup -----------------------------------------------------------------

$repoRoot = Split-Path -Parent $PSScriptRoot
$gameRoot = (Resolve-Path -LiteralPath $GameDir).Path
$systemDir = Join-Path $gameRoot 'System\Win64_AMD64'
$exePath = Join-Path $systemDir 'SbzEngine.exe'
$cfgPath = Join-Path $gameRoot 'Environment.cfg'
$logPath = Join-Path $systemDir 'BigFix.log'
$shotDir = Join-Path $gameRoot 'ScreenShots'
$installPs1 = Join-Path $repoRoot 'installer\install.ps1'

if (-not $PluginDir) { $PluginDir = Join-Path $repoRoot 'build\Release' }
if (-not (Test-Path -LiteralPath $exePath -PathType Leaf)) {
    throw "SbzEngine.exe not found: $exePath"
}

$buildPath = $null
if (Test-Path -LiteralPath $PluginDir -PathType Container) { $buildPath = (Resolve-Path -LiteralPath $PluginDir).Path }
$loaderDllPath = $null
$backupDir = $null
$staging = $null
$shortcutDir = $null
$proc = $null
$installRan = $false
$beforeMode = $null
$duringMode = $null
$afterMode = $null
$existingShots = @()
$captureInputDetail = 'capture key not sent: no first frame'

try {
    $beforeMode = Get-DesktopMode
    Write-Host ("Desktop mode before: {0}" -f $beforeMode)

    # Back up Environment.cfg (install.ps1 merges into it).
    $backupDir = Join-Path ([System.IO.Path]::GetTempPath()) ('bigfix-smoke-cfg-' + [guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $backupDir | Out-Null
    $cfgBackup = $null
    if (Test-Path -LiteralPath $cfgPath -PathType Leaf) {
        $cfgBackup = Join-Path $backupDir 'Environment.cfg'
        Copy-Item -LiteralPath $cfgPath -Destination $cfgBackup
    }

    if (-not $NoInstall) {
        if (-not $buildPath) { throw "Plugin folder not found: $PluginDir (build Release first, or use -NoInstall)" }
        $loaderDllPath = Resolve-LoaderDll -Path $LoaderDll -BuildPath $buildPath -RepoRoot $repoRoot
        $staging = New-StagingDir -BuildPath $buildPath -LoaderDllPath $loaderDllPath -DefaultIni (Join-Path $repoRoot 'installer\BigFix.default.ini')
        $shortcutDir = Join-Path $staging 'shortcuts'
        New-Item -ItemType Directory -Path $shortcutDir | Out-Null
        Write-Host ("Installing from {0} into {1}" -f $staging, $gameRoot)
        $installRan = $true
        & $installPs1 -GameDir $gameRoot -SourceDir $staging -ShortcutDir $shortcutDir
        if ($LASTEXITCODE -ne 0) { throw "install.ps1 failed with exit code $LASTEXITCODE" }
    }

    # Start from a fresh log so plugin_loaded describes this run.
    if ($TraceVideo) { Enable-VideoTrace -Path (Join-Path $systemDir 'BigFix.ini') }
    $msaaOptions = @{}
    if ($PSBoundParameters.ContainsKey('AntiAliasing')) { $msaaOptions['AntiAliasing'] = $AntiAliasing }
    if ($TracePixelFormat) { $msaaOptions['TracePixelFormat'] = $true }
    if ($msaaOptions.Count) { Set-MsaaOptions -Path (Join-Path $systemDir 'BigFix.ini') @msaaOptions }
    if ($TraceInput) { Enable-InputTrace -Path (Join-Path $systemDir 'BigFix.ini') }
    if (Test-Path -LiteralPath $logPath) { Remove-Item -LiteralPath $logPath -Force }

    $existingShots = @()
    if (Test-Path -LiteralPath $shotDir -PathType Container) {
        $existingShots = @(Get-ChildItem -LiteralPath $shotDir -Filter 'BigFix_*.png' | ForEach-Object { $_.Name })
    }

    Write-Host ("Launching {0}" -f $exePath)
    $proc = Start-Process -FilePath $exePath -WorkingDirectory $systemDir -PassThru
    $focused = Set-GameForeground -Process $proc
    if (-not $focused) { Write-Host 'Warning: could not confirm foreground focus; continuing.' }

    # Window creation is earlier than renderer readiness. Capture only after a
    # successful SwapBuffers has reported its viewport.
    $sawFirstFrame = Wait-FirstFrame -Process $proc -LogPath $logPath -Pattern $Patterns.FirstFrame -Seconds $Seconds
    $duringMode = Get-DesktopMode
    Write-Host ("Desktop mode during: {0}" -f $duringMode)

    if (-not $proc.HasExited) {
        if ($sawFirstFrame) {
            $menuReady = Wait-IntroThenMenu -Process $proc
            Add-Check -Name 'intro_escape' -Ok $menuReady -Detail 'one ESC after 20 s of intro, then 5 s for menu'
            # Readiness may take long enough for the terminal to regain focus.
            $focused = Set-GameForeground -Process $proc
            Add-Check -Name 'capture_foreground' -Ok $focused -Detail ('game window {0}; foreground {1}' -f
                $proc.MainWindowHandle, [BigFixFocus]::GetForegroundWindow())
            $captureInputDetail = 'capture key not sent: game window is not foreground'
            if ($focused -and $menuReady) {
                $sent = Send-CaptureKey -VirtualKey $CaptureKeyVk
                $captureInputDetail = 'SendInput failed for capture key'
                if ($sent) {
                    $captureInputDetail = 'SendInput capture key sent; waited up to 10 s'
                    [void](Wait-NewCapture -ShotDir $shotDir -ExistingShots $existingShots -Seconds 10)
                    if ($MouseBurst) {
                        $burstSent = Send-MouseBurst -Process $proc
                        Add-Check -Name 'mouse_burst_sent' -Ok $burstSent -Detail '2000 relative circle/reverse moves; see counts above and TraceInput totals'
                    }
                }
            }
        }
        Stop-GameProcess -Process $proc
    }

    $afterMode = Get-DesktopMode
    Write-Host ("Desktop mode after:  {0}" -f $afterMode)
    $logText = Read-LogText -Path $logPath
} finally {
    if ($Restore) {
        Write-Host 'Restoring Environment.cfg and uninstalling.'
        if ($cfgBackup -and (Test-Path -LiteralPath $cfgBackup -PathType Leaf)) {
            Copy-Item -LiteralPath $cfgBackup -Destination $cfgPath -Force
        } elseif (-not $cfgBackup -and (Test-Path -LiteralPath $cfgPath -PathType Leaf)) {
            # There was no Environment.cfg before the run; the game just made one.
            Remove-Item -LiteralPath $cfgPath -Force
        }
        if (-not $installRan) {
            Write-Host 'Skipping uninstall: this run did not install the plugin (-NoInstall).'
        } elseif ($shortcutDir) {
            & $installPs1 -GameDir $gameRoot -Uninstall -ShortcutDir $shortcutDir
        } else {
            & $installPs1 -GameDir $gameRoot -Uninstall
        }
    }
    if ($proc -and -not $proc.HasExited) { Stop-GameProcess -Process $proc }
    if ($staging) { Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue }
    if ($backupDir) { Remove-Item -LiteralPath $backupDir -Recurse -Force -ErrorAction SilentlyContinue }
}

# --- checks ----------------------------------------------------------------

$logExists = Test-Path -LiteralPath $logPath -PathType Leaf
$versionOk = $logExists -and ($logText -match $Patterns.VersionHeader)
Add-Check -Name 'plugin_loaded' -Ok $versionOk -Detail ("log {0}" -f $(if ($logExists) { $logPath } else { 'missing' }))

$buildOk = $logText -match $Patterns.Build
Add-Check -Name 'build_EU11' -Ok $buildOk -Detail ('match: ' + $Patterns.Build)

$textureOk = ($logText -match $Patterns.TexturePatch) -and ($logText -match $Patterns.TextureApplied)
Add-Check -Name 'texture_patch' -Ok $textureOk -Detail ('match: ' + $Patterns.TexturePatch + ' + ' + $Patterns.TextureApplied)

$modes = Get-ModesFilteredCheck -LogText $logText -Patterns $Patterns -DesktopMode $beforeMode
Add-Check -Name 'modes_filtered' -Ok $modes.Ok -Detail $modes.Detail

$borderlessOk = $logText -match $Patterns.Borderless
Add-Check -Name 'borderless' -Ok $borderlessOk -Detail ('match: ' + $Patterns.Borderless)

$firstFrameOk = Test-FirstFrameNative -LogText $logText -Pattern $Patterns.FirstFrame -DesktopMode $beforeMode
Add-Check -Name 'first_frame_native' -Ok $firstFrameOk -Detail ('desktop {0}; match: {1}' -f $beforeMode, $Patterns.FirstFrame)

$newShots = @()
if (Test-Path -LiteralPath $shotDir -PathType Container) {
    $newShots = @(Get-ChildItem -LiteralPath $shotDir -Filter 'BigFix_*.png' |
            Where-Object { $existingShots -notcontains $_.Name } |
            Sort-Object -Property LastWriteTime)
}
$captureOk = $false
$captureDetail = 'no new BigFix_*.png'
if ($beforeMode) {
    $parts = $beforeMode -split '[x@]'
    $desktopWidth = [int]$parts[0]
    $desktopHeight = [int]$parts[1]
    foreach ($shot in $newShots) {
        $size = Get-PngSize -Path $shot.FullName
        if ($size -and $size.Width -eq $desktopWidth -and $size.Height -eq $desktopHeight) {
            $captureOk = $true
            $captureDetail = ('{0} is {1}x{2}' -f $shot.Name, $size.Width, $size.Height)
            break
        }
    }
    if (-not $captureOk -and $newShots.Count -gt 0) {
        $size = Get-PngSize -Path $newShots[0].FullName
        $captureDetail = ('{0} is not {1}x{2}' -f $newShots[0].Name, $desktopWidth, $desktopHeight)
        if ($size) { $captureDetail = ('{0} is {1}x{2}, desktop {3}x{4}' -f $newShots[0].Name, $size.Width, $size.Height, $desktopWidth, $desktopHeight) }
    } elseif (-not $captureOk) {
        $captureDetail = ('no new BigFix_*.png in ' + $shotDir)
    }
}
if (-not $captureOk) {
    $captureDetail = Get-CaptureFailureDetail -LogText $logText -DefaultDetail ($captureDetail + '; ' + $captureInputDetail)
}
Add-Check -Name 'capture_png' -Ok $captureOk -Detail $captureDetail

$desktopOk = $beforeMode -and ($beforeMode -eq $duringMode) -and ($beforeMode -eq $afterMode)
Add-Check -Name 'desktop_unchanged' -Ok $desktopOk -Detail ('{0} / {1} / {2}' -f $beforeMode, $duringMode, $afterMode)

Write-Host ''
if ($script:Failed -eq 0) {
    Write-Host ('All {0} checks passed.' -f $script:Checks.Count)
    exit 0
}
Write-Host ('{0} of {1} checks failed.' -f $script:Failed, $script:Checks.Count)
exit 1

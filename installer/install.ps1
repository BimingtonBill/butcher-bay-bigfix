<#
.SYNOPSIS
    Installs or removes BigFix for The Chronicles of Riddick: Escape From Butcher Bay.

.DESCRIPTION
    Copies BigFix.asi, BigFix.ini and the Ultimate ASI Loader (winmm.dll) into
    <game>\System\Win64_AMD64, repairs the truncated InstallPath that the original
    2004 installer writes to the registry, merges Environment.cfg, and creates a
    Start-menu shortcut.

    Every file that is replaced is backed up under System\Win64_AMD64\.bigfix-backup
    and -Uninstall puts the game folder back byte-for-byte. A manifest at
    <game>\.bigfix-manifest.json records what was done.

.PARAMETER GameDir
    The game folder (the one containing System\Win64_AMD64\SbzEngine.exe).
    Default: the registry InstallPath, with repair of the truncated path, else a prompt.

.PARAMETER Uninstall
    Reverse a previous install using .bigfix-manifest.json.

.PARAMETER WhatIf
    Same as -DryRun: report what would change and change nothing.

.PARAMETER DryRun
    Report what would change and change nothing (no files, registry or shortcut).

.PARAMETER NoRegistryRepair
    Refuse to repair the truncated registry path instead of fixing it.

.PARAMETER RegistryFile
    Test hook: a JSON map of registry key path -> { valueName: value } to use instead
    of the real registry. Disables elevation.

.PARAMETER ShortcutDir
    Where to create the Start-menu shortcut. Default: the user's Programs folder.

.PARAMETER SourceDir
    Folder holding BigFix.asi, BigFix.ini and winmm.dll. Default: this script's folder.

.PARAMETER DataFile
    Fingerprint data file. Default: bigfix-data.json next to this script.

.PARAMETER DesktopMode
    WIDTHxHEIGHT@HZ, e.g. 2560x1440@180. Default: query the current desktop mode.

.PARAMETER Inspect
    Return read-only game detection and settings as JSON, without prompting.

.PARAMETER IniOverrides
    Validated BigFix settings to merge into the payload ini before installing.

.PARAMETER ShaderMode
    Environment.cfg shader mode: 5 (2.0, default) or 6 (2.0++).
#>
param(
    [string]$GameDir,
    [switch]$Uninstall,
    [switch]$WhatIf,
    [switch]$DryRun,
    [switch]$NoRegistryRepair,
    [string]$RegistryFile,
    [string]$ShortcutDir,
    [string]$SourceDir,
    [string]$DataFile,
    [string]$DesktopMode,
    [switch]$Inspect,
    [hashtable]$IniOverrides = @{},
    [ValidateSet(5, 6)][int]$ShaderMode = 5,
    [ValidateSet('system', 'dsoal')][string]$AudioBackend = 'system',
    [switch]$BackupAudioConflicts,
    # Internal: used by the elevated child that repairs the registry.
    [switch]$RepairOnly,
    [string]$OldPath,
    [string]$NewPath
)

$ErrorActionPreference = 'Stop'
$AudioBackend = $AudioBackend.ToLowerInvariant()

try {
    [Console]::OutputEncoding = [System.Text.UTF8Encoding]::new($false)
} catch {
}

$script:StarbreezeKey = 'HKLM\SOFTWARE\WOW6432Node\Starbreeze\The Chronicles of Riddick: Escape From Butcher Bay'
$script:UninstallKey = 'HKLM\SOFTWARE\WOW6432Node\Microsoft\Windows\CurrentVersion\Uninstall\{A8DE8C34-7F51-4cc8-B326-C425793EE741}'
$script:BackupDirName = '.bigfix-backup'
$script:ManifestName = '.bigfix-manifest.json'
$script:SystemRel = 'System\Win64_AMD64'
$script:EngineRel = 'System\Win64_AMD64\SbzEngine.exe'
$script:ConfigName = 'Environment.cfg'
$script:ShortcutName = 'The Chronicles of Riddick - Escape From Butcher Bay (BigFix).lnk'

$dry = [bool]($DryRun -or $WhatIf)
$script:RegistryMode = 'real'
if ($RegistryFile) { $script:RegistryMode = 'json' }
$script:RegistryRepairs = @()
$script:AudioEnvironment = @{}
. (Join-Path $PSScriptRoot 'audio-payload.ps1')

# --------------------------------------------------------------------------- I/O

function Write-Line([string]$Text) {
    [Console]::Out.WriteLine($Text)
}

function Fail([string]$Message) {
    [Console]::Error.WriteLine("error: $Message")
    exit 1
}

# ------------------------------------------------------- registry abstraction

function New-InsensitiveMap {
    return New-Object System.Collections.Hashtable -ArgumentList @([System.StringComparer]::OrdinalIgnoreCase)
}

function ConvertTo-InsensitiveMap($Value) {
    if ($null -eq $Value) { return $null }
    if ($Value -is [System.Management.Automation.PSCustomObject]) {
        $map = New-InsensitiveMap
        foreach ($property in $Value.PSObject.Properties) {
            $map[$property.Name] = ConvertTo-InsensitiveMap $property.Value
        }
        return $map
    }
    if ($Value -is [System.Collections.IDictionary]) {
        $map = New-InsensitiveMap
        foreach ($key in $Value.Keys) {
            $map[[string]$key] = ConvertTo-InsensitiveMap $Value[$key]
        }
        return $map
    }
    if ($Value -is [System.Collections.IEnumerable] -and $Value -isnot [string]) {
        $list = @()
        foreach ($item in $Value) { $list += ,(ConvertTo-InsensitiveMap $item) }
        return ,$list
    }
    return $Value
}

function Open-RegistryFile {
    $raw = '{}'
    if (Test-Path -LiteralPath $RegistryFile -PathType Leaf) {
        $raw = [System.IO.File]::ReadAllText($RegistryFile)
    }
    if ([string]::IsNullOrWhiteSpace($raw)) { $raw = '{}' }
    $map = ConvertTo-InsensitiveMap (ConvertFrom-Json -InputObject $raw)
    if ($null -eq $map) { $map = New-InsensitiveMap }
    $script:RegistryData = $map
}

function Save-RegistryFile {
    if ($script:RegistryMode -ne 'json') { return }
    $json = ConvertTo-Json -InputObject $script:RegistryData -Depth 10
    [System.IO.File]::WriteAllText($RegistryFile, $json + "`n", [System.Text.UTF8Encoding]::new($false))
}

function Open-RealRegistryKey([string]$KeyPath, [bool]$Writable) {
    if (-not $KeyPath.StartsWith('HKLM\')) { throw "only HKLM keys are supported: $KeyPath" }
    $subKey = $KeyPath.Substring(5)
    $base = [Microsoft.Win32.RegistryKey]::OpenBaseKey(
        [Microsoft.Win32.RegistryHive]::LocalMachine,
        [Microsoft.Win32.RegistryView]::Registry32)
    try {
        return $base.OpenSubKey($subKey, $Writable)
    } finally {
        $base.Close()
    }
}

function Get-RegistryValue([string]$KeyPath, [string]$Name) {
    if ($script:RegistryMode -eq 'json') {
        if (-not $script:RegistryData.ContainsKey($KeyPath)) { return $null }
        $values = $script:RegistryData[$KeyPath]
        if ($values -isnot [System.Collections.IDictionary]) { return $null }
        if (-not $values.ContainsKey($Name)) { return $null }
        return [string]$values[$Name]
    }
    $key = Open-RealRegistryKey $KeyPath $false
    if ($null -eq $key) { return $null }
    try {
        $value = $key.GetValue($Name, $null)
        if ($null -eq $value) { return $null }
        return [string]$value
    } finally {
        $key.Close()
    }
}

function Set-RegistryValue([string]$KeyPath, [string]$Name, [string]$Value) {
    if ($script:RegistryMode -eq 'json') {
        if (-not $script:RegistryData.ContainsKey($KeyPath)) {
            $script:RegistryData[$KeyPath] = New-InsensitiveMap
        }
        $script:RegistryData[$KeyPath][$Name] = $Value
        return
    }
    $key = Open-RealRegistryKey $KeyPath $true
    if ($null -eq $key) {
        throw "cannot open $KeyPath for writing; run as administrator"
    }
    try {
        $key.SetValue($Name, $Value, [Microsoft.Win32.RegistryValueKind]::String)
    } finally {
        $key.Close()
    }
}

function Test-IsAdmin {
    $identity = [System.Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object System.Security.Principal.WindowsPrincipal -ArgumentList @($identity)
    return $principal.IsInRole([System.Security.Principal.WindowsBuiltInRole]::Administrator)
}

# --------------------------------------------------------------- path repair

function Get-RepairedValue([string]$Value, [string]$OldBase, [string]$NewBase) {
    if ($null -eq $Value) { return $null }
    if ($Value.Equals($OldBase, [System.StringComparison]::OrdinalIgnoreCase)) { return $NewBase }
    if ($Value.StartsWith($OldBase, [System.StringComparison]::OrdinalIgnoreCase)) {
        return $NewBase + $Value.Substring($OldBase.Length)
    }
    return $Value
}

function Invoke-RegistryRepair([string]$OldBase, [string]$NewBase) {
    $targets = @(
        @{ Key = $script:StarbreezeKey; Name = 'InstallPath' },
        @{ Key = $script:UninstallKey; Name = 'InstallLocation' },
        @{ Key = $script:UninstallKey; Name = 'DisplayIcon' },
        @{ Key = $script:UninstallKey; Name = 'UninstallString' }
    )
    foreach ($target in $targets) {
        $old = Get-RegistryValue $target.Key $target.Name
        if ($null -eq $old) { continue }
        $new = Get-RepairedValue $old $OldBase $NewBase
        if ($new -eq $old) { continue }
        Set-RegistryValue $target.Key $target.Name $new
        $script:RegistryRepairs += [ordered]@{
            key = $target.Key
            name = $target.Name
            old = $old
            new = $new
        }
        Write-Line ("  repaired {0} in {1}: {2} -> {3}" -f $target.Name, $target.Key, $old, $new)
    }
    Save-RegistryFile
}

function Invoke-ElevatedRepair([string]$OldBase, [string]$NewBase) {
    $hostExe = (Get-Process -Id $PID).Path
    $argumentLine = '-NoProfile -File "{0}" -RepairOnly -OldPath "{1}" -NewPath "{2}"' -f `
        $PSCommandPath, $OldBase, $NewBase
    try {
        $child = Start-Process -FilePath $hostExe -ArgumentList $argumentLine -Verb RunAs -Wait -PassThru
        return ($child.ExitCode -eq 0)
    } catch {
        Write-Line ("Could not elevate to repair the registry: " + $_.Exception.Message)
        return $false
    }
}

# ------------------------------------------------------------- game locating

function Test-GameFolder([string]$Path) {
    if ([string]::IsNullOrWhiteSpace($Path)) { return $false }
    try {
        if (-not (Test-Path -LiteralPath $Path -PathType Container)) { return $false }
        return (Test-Path -LiteralPath (Join-Path $Path $script:EngineRel) -PathType Leaf)
    } catch {
        return $false
    }
}

function Find-GameSibling([string]$TruncatedPath) {
    $trimmed = $TruncatedPath.TrimEnd('\', '/')
    $parent = [System.IO.Path]::GetDirectoryName($trimmed)
    $leaf = [System.IO.Path]::GetFileName($trimmed)
    if ([string]::IsNullOrEmpty($parent) -or [string]::IsNullOrEmpty($leaf)) { return $null }
    if (-not (Test-Path -LiteralPath $parent -PathType Container)) { return $null }
    $candidates = @()
    foreach ($directory in @(Get-ChildItem -LiteralPath $parent -Directory -ErrorAction SilentlyContinue)) {
        $name = $directory.Name
        if ($name.Length -ne ($leaf.Length + 1)) { continue }
        if (-not $name.StartsWith($leaf, [System.StringComparison]::OrdinalIgnoreCase)) { continue }
        if (Test-GameFolder $directory.FullName) { $candidates += $directory.FullName }
    }
    if ($candidates.Count -eq 1) { return $candidates[0] }
    return $null
}

function Resolve-GameFolder([switch]$NoPrompt) {
    if ($GameDir) {
        if (Test-GameFolder $GameDir) { return (Resolve-Path -LiteralPath $GameDir).Path }
        Fail "Game folder '$GameDir' does not contain $script:EngineRel."
    }
    $registryPath = Get-RegistryValue $script:StarbreezeKey 'InstallPath'
    if ($registryPath) {
        $trimmed = $registryPath.TrimEnd('\', '/')
        if (Test-GameFolder $trimmed) { return (Resolve-Path -LiteralPath $trimmed).Path }
        $sibling = Find-GameSibling $trimmed
        if ($null -ne $sibling) { return $sibling }
        Fail ("Registry InstallPath '$registryPath' is not a game folder and no folder named like it " +
              "plus one character was found next to it. Pass -GameDir <path>.")
    }
    if ($NoPrompt -or [Console]::IsInputRedirected) {
        Fail 'No game folder found. Pass -GameDir <path> to the folder holding System\Win64_AMD64\SbzEngine.exe.'
    }
    $answer = Read-Host 'Path to the game folder (the one containing System\Win64_AMD64\SbzEngine.exe)'
    if (Test-GameFolder $answer) { return (Resolve-Path -LiteralPath $answer).Path }
    Fail "Game folder '$answer' does not contain $script:EngineRel."
}

# The truncated-path repair is independent of how the game folder was found: it
# compares the registry InstallPath with the folder we resolved and fixes the
# registry whenever that path is a strict prefix of it and no such folder exists.
function Get-RegistryRepairPlan([string]$GameFolder) {
    $registryPath = Get-RegistryValue $script:StarbreezeKey 'InstallPath'
    if (-not $registryPath) { return $null }
    $oldBase = $registryPath.TrimEnd('\', '/')
    if ([string]::IsNullOrEmpty($oldBase)) { return $null }
    if (Test-Path -LiteralPath $oldBase -PathType Container) { return $null }
    $newBase = $GameFolder.TrimEnd('\', '/')
    if ($newBase.Length -le $oldBase.Length) { return $null }
    if (-not $newBase.StartsWith($oldBase, [System.StringComparison]::OrdinalIgnoreCase)) { return $null }
    return @{ oldBase = $oldBase; newBase = $newBase; registryPath = $registryPath }
}

function Repair-TruncatedRegistryPath([string]$GameFolder) {
    $plan = Get-RegistryRepairPlan $GameFolder
    if ($null -eq $plan) { return }
    Write-Line "Registry InstallPath is truncated: $($plan.registryPath)"
    Write-Line "Found the game at: $GameFolder"
    if ($NoRegistryRepair) {
        Write-Line 'Leaving the truncated registry path alone because -NoRegistryRepair was given.'
        return
    }
    if ($dry) {
        Write-Line "Dry run: would repair InstallPath and the uninstall key to '$($plan.newBase)\'."
        return
    }
    if ($script:RegistryMode -eq 'real' -and -not (Test-IsAdmin)) {
        Write-Line 'Repairing the registry needs administrator rights; asking for elevation.'
        if (-not (Invoke-ElevatedRepair $plan.oldBase $plan.newBase)) {
            Fail 'The registry was not repaired. Re-run this installer as administrator.'
        }
        Write-Line 'Registry repaired (elevated).'
        return
    }
    Invoke-RegistryRepair $plan.oldBase $plan.newBase
    Write-Line 'Registry repaired.'
}

# -------------------------------------------------------------- build detect

function Get-FileSha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Find-Build($Data, [string]$GameFolder) {
    $required = @($Data['patchedFiles'])
    if ($required.Count -eq 0 -or -not $required[0]) {
        Fail 'Fingerprint data has no patchedFiles safety policy; update BigFix. No files were changed.'
    }
    $hashes = @{}
    $best = $null
    $bestCount = -1
    foreach ($build in @($Data['builds'])) {
        $files = $build['files']
        $different = @()
        $matched = 0
        foreach ($relative in $files.Keys) {
            $full = Join-Path $GameFolder ($relative -replace '/', '\')
            if (-not $hashes.ContainsKey($relative)) {
                try {
                    $hashes[$relative] = if (Test-Path -LiteralPath $full -PathType Leaf) { Get-FileSha256 $full } else { '' }
                } catch { $hashes[$relative] = 'unreadable' }
            }
            $actual = $hashes[$relative]
            $expected = ([string]$files[$relative]).ToLowerInvariant()
            if (-not $actual -or $expected -notmatch '^[a-f0-9]{64}$' -or $actual -ne $expected) {
                $different += $relative
            } else { $matched++ }
        }
        $badRequired = @($required | Where-Object { -not $files.ContainsKey($_) -or $_ -in $different })
        $safe = ($build['supported'] -eq $true -and $badRequired.Count -eq 0)
        if (($safe -or ($build['id'] -eq 'EU10' -and $different.Count -eq 0)) -and $matched -gt $bestCount) {
            $best = @{ id = $build['id']; label = $build['label']; different = $different }
            $bestCount = $matched
        }
        if (-not $safe -and $build['supported'] -eq $true) {
            Write-Line ("Against {0}, unverified or missing patched files: {1}" -f $build['id'], ($badRequired -join ', '))
        }
    }
    return $best
}

# ----------------------------------------------------------- Environment.cfg

function Get-DesktopMode {
    param([switch]$IgnoreOverride, [switch]$Quiet)
    if ($DesktopMode -and -not $IgnoreOverride) {
        if ($DesktopMode -notmatch '^\s*(\d+)\s*[xX]\s*(\d+)(?:\s*@\s*(\d+))?\s*$') {
            Fail "Invalid -DesktopMode '$DesktopMode'; expected WIDTHxHEIGHT@HZ, e.g. 2560x1440@180."
        }
        $hz = 60
        if ($matches[3]) { $hz = [int]$matches[3] }
        return @{ width = [int]$matches[1]; height = [int]$matches[2]; hz = $hz }
    }
    try {
        $controller = Get-CimInstance -ClassName Win32_VideoController -ErrorAction Stop |
            Where-Object { $_.CurrentHorizontalResolution -and $_.CurrentVerticalResolution } |
            Select-Object -First 1
        if ($null -ne $controller) {
            $hz = 60
            if ($controller.CurrentRefreshRate) { $hz = [int]$controller.CurrentRefreshRate }
            return @{
                width  = [int]$controller.CurrentHorizontalResolution
                height = [int]$controller.CurrentVerticalResolution
                hz     = $hz
            }
        }
    } catch {
    }
    if (-not $Quiet) { Write-Line 'Warning: could not query the desktop resolution; using 1920x1080@60. Pass -DesktopMode to override.' }
    return @{ width = 1920; height = 1080; hz = 60 }
}

function Merge-EnvironmentCfg([string]$Existing, [hashtable]$Mode) {
    $managedKeys = @('VID_MODE', 'VID_DWIDTH', 'VID_DHEIGHT', 'XR_SHADERMODE', 'VID_VSYNC')
    $managed = New-InsensitiveMap
    $managed['VID_MODE'] = ('{0} {1} 32 {2}' -f $Mode.width, $Mode.height, $Mode.hz)
    $managed['VID_DWIDTH'] = [string]$Mode.width
    $managed['VID_DHEIGHT'] = [string]$Mode.height
    $managed['XR_SHADERMODE'] = [string]$ShaderMode
    $managed['VID_VSYNC'] = '1'
    foreach ($key in $script:AudioEnvironment.Keys) {
        $managedKeys += $key
        $managed[$key] = [string]$script:AudioEnvironment[$key]
    }

    $seen = New-InsensitiveMap
    $lines = New-Object System.Collections.Generic.List[string]
    $newline = "`r`n"
    $trailingNewline = $true

    if (-not [string]::IsNullOrEmpty($Existing)) {
        if (-not $Existing.Contains("`r`n")) { $newline = "`n" }
        $trailingNewline = $Existing.EndsWith("`n")
        $body = $Existing
        if ($trailingNewline) {
            $body = $body.Substring(0, $body.Length - 1)
            if ($body.EndsWith("`r")) { $body = $body.Substring(0, $body.Length - 1) }
        }
        if ($body.Length -gt 0) {
            foreach ($line in ($body -split "\r?\n")) {
                $replaced = $false
                if ($line -match '^\s*([^=\s#;][^=]*?)\s*=(.*)$') {
                    $key = $matches[1].Trim()
                    if ($managed.ContainsKey($key)) {
                        $lines.Add($key.ToUpperInvariant() + '=' + $managed[$key])
                        $seen[$key] = $true
                        $replaced = $true
                    }
                }
                if (-not $replaced) { $lines.Add($line) }
            }
        }
    }

    foreach ($key in $managedKeys) {
        if (-not $seen.ContainsKey($key)) { $lines.Add($key + '=' + $managed[$key]) }
    }

    $text = ($lines -join $newline)
    if ($trailingNewline -or [string]::IsNullOrEmpty($Existing)) { $text += $newline }
    return $text
}

# ---------------------------------------------------------------- shortcut

function New-StartMenuShortcut([string]$Path, [string]$Target, [string]$WorkingDirectory) {
    $shell = New-Object -ComObject WScript.Shell
    try {
        $link = $shell.CreateShortcut($Path)
        $link.TargetPath = $Target
        $link.WorkingDirectory = $WorkingDirectory
        $link.Description = 'The Chronicles of Riddick: Escape From Butcher Bay (BigFix)'
        $link.Save()
    } finally {
        [void][System.Runtime.InteropServices.Marshal]::ReleaseComObject($shell)
    }
}

function Get-ShortcutFolder {
    if ($ShortcutDir) { return $ShortcutDir }
    return (Join-Path ([Environment]::GetFolderPath('Programs')) 'BigFix')
}

# ------------------------------------------------------------------- install

# Settings belong here, beside the file writer, so CLI and GUI have the same
# validation, backup and uninstall behavior. Unknown payload keys are retained.
function Assert-IniOverrides {
    $booleans = @('RawMouse', 'TextureSizeFix', 'FixMiscBugs', 'Enable20PlusPlusAnyGpu',
        'EyeshineFix', 'EyeshineFlickerWorkaround', 'DisplayModeFilter', 'HudAspectFix',
        'VideoAspectFix', 'SkipLogoVideos', 'BackgroundAudio', 'CapturePng', 'TraceEax')
    foreach ($key in $IniOverrides.Keys) {
        $value = [string]$IniOverrides[$key]
        if ($key -in $booleans) {
            if ($value -notin @('0', '1')) { Fail "$key must be 0 or 1." }
        } elseif ($key -in @('FOV', 'FrameCap')) {
            $number = 0.0
            if (-not [double]::TryParse($value, [Globalization.NumberStyles]::Float,
                    [Globalization.CultureInfo]::InvariantCulture, [ref]$number) -or
                [double]::IsNaN($number) -or [double]::IsInfinity($number)) {
                Fail "$key must be a finite number."
            }
            if ($key -eq 'FOV' -and $number -ne 0 -and ($number -lt 60 -or $number -gt 120)) {
                Fail 'FOV must be 0 (original) or 60 to 120.'
            }
            if ($key -eq 'FrameCap' -and $number -ne 0 -and $number -lt 1) {
                Fail 'FrameCap must be 0 (uncapped) or at least 1.'
            }
        } elseif ($key -eq 'AntiAliasing') {
            if ($value -notin @('0', '2', '4', '8')) { Fail 'AntiAliasing must be 0, 2, 4 or 8.' }
        } elseif ($key -eq 'WindowMode') {
            if ($value -notin @('borderless', 'fullscreen', 'windowed')) { Fail 'Invalid WindowMode.' }
        } else {
            Fail "Unsupported setting: $key"
        }
    }
}

function Read-BigFixSettings([string]$Path) {
    $settings = @{}
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $settings }
    $active = $false
    foreach ($line in [IO.File]::ReadAllLines($Path)) {
        if ($line -match '^\s*\[(.+)\]\s*$') { $active = $matches[1] -in @('BigFix', 'General') }
        elseif ($active -and $line -match '^\s*([^;#=\s][^=]*?)\s*=\s*(.*?)\s*$') {
            $settings[$matches[1]] = $matches[2]
        }
    }
    return $settings
}

function Merge-BigFixIni([string]$Text) {
    $remaining = @{} + $IniOverrides
    $lines = New-Object System.Collections.Generic.List[string]
    $active = $false
    foreach ($line in ($Text -split '\r?\n')) {
        if ($line -match '^\s*\[(.+)\]\s*$') { $active = $matches[1] -in @('BigFix', 'General') }
        if ($active -and $line -match '^\s*([^;#=\s][^=]*?)\s*=') {
            $key = $matches[1]
            if ($IniOverrides.ContainsKey($key)) {
                $line = "$key=$($IniOverrides[$key])"
                $remaining.Remove($key)
            }
        }
        $lines.Add($line)
    }
    if ($remaining.Count) {
        $lines.Add('[BigFix]')
        foreach ($key in @($remaining.Keys | Sort-Object)) { $lines.Add("$key=$($remaining[$key])") }
    }
    return ($lines -join "`r`n") + "`r`n"
}

function Get-InstallData {
    $path = $DataFile
    if (-not $path) { $path = Join-Path $PSScriptRoot 'bigfix-data.json' }
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { Fail "Data file not found: $path" }
    return ConvertTo-InsensitiveMap (ConvertFrom-Json -InputObject ([IO.File]::ReadAllText($path)))
}

function Write-Inspection {
    $folder = Resolve-GameFolder -NoPrompt
    $data = Get-InstallData
    $build = Find-Build $data $folder
    $id = if ($null -ne $build) { [string]$build['id'] } else { $null }
    $status = if ($id -eq 'EU10') { 'needs 1.1 patch' } elseif ($id) { 'known' } else { 'unknown' }
    $ini = Join-Path (Join-Path $folder $script:SystemRel) 'BigFix.ini'
    $desktop = Get-DesktopMode -IgnoreOverride -Quiet
    $manifestPath = Join-Path $folder $script:ManifestName
    $manifest = if (Test-Path -LiteralPath $manifestPath -PathType Leaf) {
        ConvertTo-InsensitiveMap (ConvertFrom-Json ([IO.File]::ReadAllText($manifestPath)))
    } else { $null }
    $source = if ($SourceDir) { $SourceDir } else { $PSScriptRoot }
    $audioNames = @('dsound.dll', 'dsoal-aldrv.dll')
    $available = @($audioNames | Where-Object { -not (Test-Path -LiteralPath (Join-Path $source $_) -PathType Leaf) }).Count -eq 0
    $owned = if ($manifest -and $manifest['files']) { @($manifest['files'] | ForEach-Object { $_['path'] }) } else { @() }
    $conflicts = @($audioNames | Where-Object {
        $destination = Join-Path (Join-Path $folder $script:SystemRel) $_
        (Test-Path -LiteralPath $destination) -and $destination -notin $owned
    })
    [ordered]@{
        GameDir = $folder
        Build = $id
        BuildLabel = if ($build -and $build['label']) { [string]$build['label'] } else { $id }
        DesktopMode = '{0}x{1}@{2}' -f $desktop.width, $desktop.height, $desktop.hz
        Status = $status
        NeedsRegistryRepair = ($null -ne (Get-RegistryRepairPlan $folder))
        Installed = (Test-Path -LiteralPath (Join-Path $folder $script:ManifestName) -PathType Leaf)
        AudioBackend = if ($manifest -and $manifest['audioBackend'] -eq 'dsoal') { 'dsoal' } else { 'system' }
        AudioPayloadAvailable = $available
        AudioConflicts = $conflicts
        Settings = (Read-BigFixSettings $ini)
    } | ConvertTo-Json -Depth 5
}

function Invoke-Install {
    Assert-IniOverrides
    $gameFolder = Resolve-GameFolder
    $data = Get-InstallData

    $sourceFolder = $SourceDir
    if (-not $sourceFolder) { $sourceFolder = $PSScriptRoot }
    if (-not (Test-Path -LiteralPath $sourceFolder -PathType Container)) {
        Fail "Source folder not found: $sourceFolder"
    }

    $build = Find-Build $data $gameFolder
    $buildId = $null
    if ($null -eq $build) {
        Write-Line 'Unrecognised build: every patched file must match one verified build. No files were changed.'
        $tool = Join-Path $PSScriptRoot 'fingerprint.ps1'
        if (-not (Test-Path -LiteralPath $tool)) { $tool = Join-Path (Split-Path $PSScriptRoot -Parent) 'tools\fingerprint.ps1' }
        $quotedGame = $gameFolder.Replace("'", "''")
        $quotedTool = $tool.Replace("'", "''")
        Write-Line "Run from outside the game folder: powershell -ExecutionPolicy Bypass -File '$quotedTool' -GameDir '$quotedGame' -Output 'bigfix-fingerprint.json'"
        Write-Line 'Attach bigfix-fingerprint.json to a new build issue using .github/ISSUE_TEMPLATE/new-build.yml. Do not upload game files.'
        exit 1
    } else {
        $buildId = [string]$build['id']
        if ($buildId -eq 'EU10') {
            Write-Line 'This is The Chronicles of Riddick 1.0 (EU DVD). BigFix needs version 1.1.'
            Write-Line 'Run the official 1.0 -> 1.1 patch (riddick_update_eu_10_11.exe) first, then run this installer again.'
            Write-Line 'The patch may need Windows XP compatibility mode on Windows 10/11, or Wine on Linux.'
            Write-Line 'No files were changed.'
            exit 2
        }
        if (@($build['different']).Count -gt 0) {
            Write-Line "Build: $buildId (partial match; every patched file matches)."
            foreach ($relative in @($build['different'])) { Write-Line "  differs or missing: $relative" }
        } else { Write-Line "Build: $buildId (recognised)." }
    }

    $systemDir = Join-Path $gameFolder $script:SystemRel
    $backupDir = Join-Path $systemDir $script:BackupDirName
    $installNames = @($data['install'])
    $manifestPath = Join-Path $gameFolder $script:ManifestName
    $previous = $null
    if (Test-Path -LiteralPath $manifestPath) {
        $previous = ConvertFrom-Json ([IO.File]::ReadAllText($manifestPath))
    }
    if (($AudioBackend -eq 'dsoal' -or ($previous -and $previous.audioBackend -eq 'dsoal')) -and $previous) {
        Fail 'Uninstall the current BigFix installation before changing the audio backend.'
    }
    if ($AudioBackend -eq 'dsoal') {
        $audio = $data['components']['dsoal']
        if (-not $audio -or @($audio.files).Count -ne 2) { Fail 'Missing pinned DSOAL component metadata.' }
        Assert-AudioPayload $sourceFolder $audio.files
        foreach ($entry in $audio.files) {
            if (Test-Path -LiteralPath (Join-Path $backupDir $entry.name)) {
                Fail "Audio conflict: an orphaned backup of $($entry.name) exists. Resolve it before installing."
            }
            if ((Test-Path -LiteralPath (Join-Path $systemDir $entry.name)) -and -not $BackupAudioConflicts) {
                Fail "Audio conflict: $($entry.name) already exists. Use -BackupAudioConflicts to back it up reversibly."
            }
            $installNames += $entry.name
        }
        $script:AudioEnvironment = $audio.environment
    }
    $installedFiles = @()
    $missing = @()
    foreach ($name in $installNames) {
        $destination = Join-Path $systemDir $name
        if ((Test-Path -LiteralPath $destination) -and -not (Test-Path -LiteralPath $destination -PathType Leaf)) {
            Fail "Install destination is not a file: $destination"
        }
        if (-not (Test-Path -LiteralPath (Join-Path $sourceFolder $name) -PathType Leaf)) {
            $missing += $name
        }
    }
    if ($missing.Count -gt 0) {
        Fail ("Missing install file(s) in '{0}': {1}" -f $sourceFolder, ($missing -join ', '))
    }

    $mode = Get-DesktopMode
    $snapshots = @{}
    $snapshotHashes = @{}
    $paths = @($installNames | ForEach-Object { Join-Path $systemDir $_ })
    $paths += (Join-Path $gameFolder $script:ConfigName), $manifestPath
    $paths += (Join-Path (Get-ShortcutFolder) $script:ShortcutName)
    # Restore live destinations before discarding any backups.
    $paths += @($installNames | ForEach-Object { Join-Path $backupDir $_ })
    $paths += (Join-Path $backupDir $script:ConfigName)
    foreach ($path in $paths) {
        $snapshots[$path] = if (Test-Path -LiteralPath $path -PathType Leaf) { [IO.File]::ReadAllBytes($path) } else { $null }
        if ($null -ne $snapshots[$path]) { $snapshotHashes[$path] = Get-FileSha256 $path }
    }
    try {
    Repair-TruncatedRegistryPath $gameFolder
    $replaced = @()
    foreach ($name in $installNames) {
        $source = Join-Path $sourceFolder $name
        $destination = Join-Path $systemDir $name
        $existed = Test-Path -LiteralPath $destination -PathType Leaf
        $backupPath = $null
        if ($existed) {
            $backupPath = Join-Path $backupDir $name
            if (Test-Path -LiteralPath $backupPath -PathType Leaf) {
                Write-Line "  keeping the existing backup of $name"
            } elseif ($dry) {
                Write-Line "  dry run: would back up $destination -> $backupPath"
            } else {
                if (-not (Test-Path -LiteralPath $backupDir -PathType Container)) {
                    New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
                }
                Copy-Item -LiteralPath $destination -Destination $backupPath -Force
                Write-Line "  backed up $name"
            }
        }
        if ($dry) {
            Write-Line "  dry run: would copy $source -> $destination"
        } elseif ($name -eq 'BigFix.ini' -and $IniOverrides.Count) {
            $text = Merge-BigFixIni ([IO.File]::ReadAllText($source))
            [IO.File]::WriteAllText($destination, $text, [Text.UTF8Encoding]::new($false))
            Write-Line "  installed $name with selected settings"
        } else {
            Copy-Item -LiteralPath $source -Destination $destination -Force
            Write-Line "  installed $name"
        }
        if ($existed) { $replaced += $name }
        $installedFiles += [ordered]@{
            name = $name
            path = $destination
            existed = $existed
            backup = $backupPath
            installedSha256 = Get-FileSha256 $source
        }
    }

    if ($dry -and $IniOverrides.Count) {
        foreach ($key in @($IniOverrides.Keys | Sort-Object)) { Write-Line "  dry run: setting $key=$($IniOverrides[$key])" }
    }

    $modeText = '{0}x{1}@{2}' -f $mode.width, $mode.height, $mode.hz
    $cfgPath = Join-Path $gameFolder $script:ConfigName
    $cfgExisted = Test-Path -LiteralPath $cfgPath -PathType Leaf
    $cfgBackup = $null
    $cfgText = $null
    if ($cfgExisted) {
        $existingText = [System.IO.File]::ReadAllText($cfgPath)
        $cfgText = Merge-EnvironmentCfg $existingText $mode
        $cfgBackup = Join-Path $backupDir $script:ConfigName
        if ($dry) {
            Write-Line "  dry run: would back up $cfgPath -> $cfgBackup"
        } elseif (-not (Test-Path -LiteralPath $cfgBackup -PathType Leaf)) {
            if (-not (Test-Path -LiteralPath $backupDir -PathType Container)) {
                New-Item -ItemType Directory -Path $backupDir -Force | Out-Null
            }
            Copy-Item -LiteralPath $cfgPath -Destination $cfgBackup -Force
        }
    } else {
        $cfgText = Merge-EnvironmentCfg '' $mode
    }
    if ($dry) {
        Write-Line "  dry run: would write $cfgPath with VID_MODE=$($mode.width) $($mode.height) 32 $($mode.hz), XR_SHADERMODE=$ShaderMode, VID_VSYNC=1"
    } else {
        [System.IO.File]::WriteAllText($cfgPath, $cfgText, [System.Text.UTF8Encoding]::new($false))
        Write-Line "  wrote $($script:ConfigName): VID_MODE=$($mode.width) $($mode.height) 32 $($mode.hz), XR_SHADERMODE=$ShaderMode, VID_VSYNC=1"
    }

    if ($AudioBackend -eq 'dsoal') { Write-Line '  audio: SND_USE3D=1, SND_USEEAX=1 (restart required)' }
    $target = Join-Path $systemDir 'SbzEngine.exe'
    $shortcutFolder = Get-ShortcutFolder
    $shortcutFolderExisted = Test-Path -LiteralPath $shortcutFolder -PathType Container
    $shortcutPath = Join-Path $shortcutFolder $script:ShortcutName
    if ($dry) {
        Write-Line "  dry run: would create the shortcut $shortcutPath -> $target"
    } else {
        if (-not $shortcutFolderExisted) {
            New-Item -ItemType Directory -Path $shortcutFolder -Force | Out-Null
        }
        New-StartMenuShortcut $shortcutPath $target $systemDir
        Write-Line "  created shortcut $shortcutPath"
    }

    if ($dry) {
        Write-Line "Dry run: no changes were made. BigFix would be installed into $systemDir."
        return
    }

    $manifest = [ordered]@{
        version            = 1
        audioBackend       = $AudioBackend
        gameDir            = $gameFolder
        systemDir          = $systemDir
        build              = $buildId
        desktopMode        = $modeText
        files              = $installedFiles
        environmentCfg     = [ordered]@{
            path    = $cfgPath
            existed = $cfgExisted
            backup  = $cfgBackup
            installedSha256 = Get-FileSha256 $cfgPath
        }
        shortcut           = $shortcutPath
        shortcutDirCreated = (-not $shortcutFolderExisted)
        registryRepairs    = @($script:RegistryRepairs)
    }
    $manifestPath = Join-Path $gameFolder $script:ManifestName
    $manifestJson = ConvertTo-Json -InputObject $manifest -Depth 10
    [System.IO.File]::WriteAllText($manifestPath, $manifestJson + "`n", [System.Text.UTF8Encoding]::new($false))
    } catch {
        if (-not $dry) {
            $restoreFailed = $false
            foreach ($path in $paths) {
                if ($restoreFailed -and $path.StartsWith($backupDir + [IO.Path]::DirectorySeparatorChar)) { continue }
                try {
                    if ($null -ne $snapshots[$path]) {
                        if ((Test-Path -LiteralPath $path -PathType Leaf) -and
                            (Get-FileSha256 $path) -eq $snapshotHashes[$path]) { continue }
                        [IO.File]::WriteAllBytes($path, $snapshots[$path])
                    } elseif (Test-Path -LiteralPath $path -PathType Leaf) { [IO.File]::Delete($path) }
                } catch {
                    $restoreFailed = $true
                    Write-Line "Rollback could not restore $path; backups retained for manual recovery."
                }
            }
            foreach ($folder in @($backupDir, (Get-ShortcutFolder))) {
                if ((Test-Path -LiteralPath $folder -PathType Container) -and -not (Get-ChildItem -LiteralPath $folder -Force | Select-Object -First 1)) {
                    [IO.Directory]::Delete($folder)
                }
            }
        }
        throw
    }

    Write-Line "BigFix installed into $systemDir."
    Write-Line "Uninstall with: install.ps1 -Uninstall -GameDir `"$gameFolder`""
}

# ----------------------------------------------------------------- uninstall

function Invoke-Uninstall {
    $gameFolder = Resolve-GameFolder
    $manifestPath = Join-Path $gameFolder $script:ManifestName
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        Fail "Nothing to uninstall: $manifestPath does not exist."
    }
    $manifest = ConvertTo-InsensitiveMap (ConvertFrom-Json -InputObject ([System.IO.File]::ReadAllText($manifestPath)))
    $backupDir = Join-Path (Join-Path $gameFolder $script:SystemRel) $script:BackupDirName

    if ($manifest['audioBackend'] -eq 'dsoal') {
        foreach ($entry in @($manifest['files']) + @($manifest['environmentCfg'])) {
            if ($entry['installedSha256'] -and (Test-Path -LiteralPath $entry['path'] -PathType Leaf) -and
                (Get-FileSha256 $entry['path']) -ne $entry['installedSha256']) {
                Fail "Uninstall conflict: $($entry['path']) changed since installation. Move it aside and retry; backups and manifest were preserved."
            }
            if ($entry['backup'] -and -not (Test-Path -LiteralPath $entry['backup'] -PathType Leaf)) {
                Fail "Uninstall conflict: missing backup $($entry['backup']); no files changed."
            }
        }
    }

    $files = $manifest['files']
    if ($null -ne $files) {
        foreach ($entry in @($files)) {
            $destination = [string]$entry['path']
            $backupPath = $entry['backup']
            if ($dry) {
                if ($backupPath) {
                    Write-Line "  dry run: would restore $destination from $backupPath"
                } else {
                    Write-Line "  dry run: would remove $destination"
                }
                continue
            }
            if ($backupPath -and (Test-Path -LiteralPath $backupPath -PathType Leaf)) {
                Copy-Item -LiteralPath $backupPath -Destination $destination -Force
                Remove-Item -LiteralPath $backupPath -Force
                Write-Line "  restored $($entry['name'])"
            } elseif (Test-Path -LiteralPath $destination -PathType Leaf) {
                Remove-Item -LiteralPath $destination -Force
                Write-Line "  removed $($entry['name'])"
            }
        }
    }

    $cfg = $manifest['environmentCfg']
    if ($null -ne $cfg) {
        $cfgPath = [string]$cfg['path']
        $cfgBackup = $cfg['backup']
        if ($dry) {
            Write-Line "  dry run: would restore or remove $cfgPath"
        } elseif ($cfg['existed'] -and $cfgBackup -and (Test-Path -LiteralPath $cfgBackup -PathType Leaf)) {
            Copy-Item -LiteralPath $cfgBackup -Destination $cfgPath -Force
            Remove-Item -LiteralPath $cfgBackup -Force
            Write-Line "  restored $($script:ConfigName)"
        } elseif (Test-Path -LiteralPath $cfgPath -PathType Leaf) {
            Remove-Item -LiteralPath $cfgPath -Force
            Write-Line "  removed $($script:ConfigName)"
        }
    }

    $shortcutPath = $manifest['shortcut']
    if ($shortcutPath -and (Test-Path -LiteralPath $shortcutPath -PathType Leaf)) {
        if ($dry) {
            Write-Line "  dry run: would remove $shortcutPath"
        } else {
            Remove-Item -LiteralPath $shortcutPath -Force
            Write-Line "  removed shortcut $shortcutPath"
        }
    }
    if (-not $dry -and $manifest['shortcutDirCreated'] -and $shortcutPath) {
        $shortcutFolder = [System.IO.Path]::GetDirectoryName([string]$shortcutPath)
        if ((Test-Path -LiteralPath $shortcutFolder -PathType Container) -and
            -not (Get-ChildItem -LiteralPath $shortcutFolder -Force | Select-Object -First 1)) {
            Remove-Item -LiteralPath $shortcutFolder -Force
        }
    }

    if (-not $dry) {
        if ((Test-Path -LiteralPath $backupDir -PathType Container) -and
            -not (Get-ChildItem -LiteralPath $backupDir -Force | Select-Object -First 1)) {
            Remove-Item -LiteralPath $backupDir -Force
        }
        Remove-Item -LiteralPath $manifestPath -Force
        Write-Line "BigFix removed from $gameFolder."
    } else {
        Write-Line 'Dry run: no changes were made.'
    }
}

# ---------------------------------------------------------------------- main

if ($script:RegistryMode -eq 'json') { Open-RegistryFile }

if ($Inspect) {
    Write-Inspection
    exit 0
}

if ($RepairOnly) {
    if (-not $OldPath -or -not $NewPath) { Fail '-RepairOnly needs -OldPath and -NewPath.' }
    if ($script:RegistryMode -eq 'json') { Fail '-RepairOnly expects the real registry.' }
    Invoke-RegistryRepair $OldPath $NewPath
    exit 0
}

if ($Uninstall) {
    Invoke-Uninstall
} else {
    Invoke-Install
}
exit 0

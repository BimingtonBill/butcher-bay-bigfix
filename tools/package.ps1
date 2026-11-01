<#
.SYNOPSIS
    Builds the release zip: BigFix-v<version>.zip in dist\.

.DESCRIPTION
    Collects the plugin, the default settings file, the Ultimate ASI Loader
    (saved as winmm.dll), the installers, both data files and the docs into one
    deterministic zip. The loader is downloaded from the pinned release and its
    sha256 is verified before anything is extracted; a mismatch aborts and
    leaves no zip behind.

    The zip entries have a fixed order (the list in $PackageFiles) and a fixed
    timestamp (1980-01-01), so packaging the same inputs twice yields identical
    bytes. The file list is the same order as the switch:

        BigFix.asi  BigFix.ini  winmm.dll  install.ps1  BigFixSetup.ps1
        BigFixSetup.cmd  install.sh
        bigfix-data.json  bigfix-data.txt  README.md  NOTICE.md  LICENSE
        docs/VERIFY.md  docs/COMPAT.md  tga2png.py

.PARAMETER BuildDir
    Folder holding the built BigFix.asi (and, if the build wrote one,
    BigFix.ini). Default: build\Release.

.PARAMETER OutDir
    Where BigFix-v<version>.zip is written. Default: dist.

.PARAMETER LoaderZip
    Use this local Ultimate ASI Loader zip instead of downloading it.
    For offline builds and tests.

.PARAMETER LoaderSha256
    Test hook: expected sha256 of the loader zip. Default: the pinned v10.0.2
    NoPDB x64 archive. Only override this in tests with a local -LoaderZip.

.EXAMPLE
    pwsh tools/package.ps1
    pwsh tools/package.ps1 -BuildDir build\Release -OutDir dist
#>
[CmdletBinding()]
param(
    [string]$BuildDir = 'build\Release',
    [string]$OutDir = 'dist',
    [string]$LoaderZip,
    [switch]$IncludeDsoal,
    [string]$DsoalZip,
    [string]$DsoalSourceDir,
    # Offline tests only; cannot redirect a download through untrusted metadata.
    [string]$DsoalDataFile,
    [string]$LoaderSha256 = 'fe964adf4de7f467ff39c440a4059de02e37d0574a62944eef2f55a35edafa2f'
)

$ErrorActionPreference = 'Stop'

$LoaderUrl = 'https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/download/v10.0.2/Ultimate-ASI-Loader-NoPDB_x64.zip'

$repoRoot = Split-Path -Parent $PSScriptRoot
$versionHeader = Join-Path $repoRoot 'src\version.h'
$defaultIni = Join-Path $repoRoot 'installer\BigFix.default.ini'

# Entry name -> source path, in the order they are written to the zip.
function Get-PackagePlan {
    param(
        [string]$RepoRoot,
        [string]$BuildPath,
        [string]$LoaderDll
    )
    $builtIni = Join-Path $BuildPath 'BigFix.ini'
    $iniSource = if (Test-Path -LiteralPath $builtIni -PathType Leaf) { $builtIni } else { $script:defaultIni }
    $plan = New-Object System.Collections.Generic.List[object]
    foreach ($item in @(
            @{ Name = 'BigFix.asi'; Path = Join-Path $BuildPath 'BigFix.asi' },
            @{ Name = 'BigFix.ini'; Path = $iniSource },
            @{ Name = 'winmm.dll'; Path = $LoaderDll },
            @{ Name = 'install.ps1'; Path = (Join-Path $RepoRoot 'installer\install.ps1') },
            @{ Name = 'BigFixSetup.ps1'; Path = (Join-Path $RepoRoot 'installer\BigFixSetup.ps1') },
            @{ Name = 'BigFixSetup.cmd'; Path = (Join-Path $RepoRoot 'installer\BigFixSetup.cmd') },
            @{ Name = 'install.sh'; Path = (Join-Path $RepoRoot 'installer\install.sh') },
            @{ Name = 'bigfix-data.json'; Path = (Join-Path $RepoRoot 'installer\bigfix-data.json') },
            @{ Name = 'bigfix-data.txt'; Path = (Join-Path $RepoRoot 'installer\bigfix-data.txt') },
            @{ Name = 'README.md'; Path = (Join-Path $RepoRoot 'README.md') },
            @{ Name = 'NOTICE.md'; Path = (Join-Path $RepoRoot 'NOTICE.md') },
            @{ Name = 'LICENSE'; Path = (Join-Path $RepoRoot 'LICENSE') },
            @{ Name = 'docs/VERIFY.md'; Path = (Join-Path $RepoRoot 'docs\VERIFY.md') },
            @{ Name = 'docs/COMPAT.md'; Path = (Join-Path $RepoRoot 'docs\COMPAT.md') },
            @{ Name = 'tga2png.py'; Path = (Join-Path $RepoRoot 'tools\tga2png.py') },
            @{ Name = 'fingerprint.py'; Path = (Join-Path $RepoRoot 'tools\fingerprint.py') },
            @{ Name = 'fingerprint.ps1'; Path = (Join-Path $RepoRoot 'tools\fingerprint.ps1') },
            @{ Name = 'fingerprint.sh'; Path = (Join-Path $RepoRoot 'tools\fingerprint.sh') }
            @{ Name = 'audio-payload.ps1'; Path = (Join-Path $RepoRoot 'installer\audio-payload.ps1') },
            @{ Name = 'audio-payload.py'; Path = (Join-Path $RepoRoot 'installer\audio-payload.py') }
            @{ Name = 'docs/audio-backend.md'; Path = (Join-Path $RepoRoot 'docs\audio-backend.md') }
        )) {
        $plan.Add([pscustomobject]$item)
    }
    return $plan
}

function Assert-FilesExist {
    param([object[]]$Plan)
    $missing = @($Plan | Where-Object { -not (Test-Path -LiteralPath $_.Path -PathType Leaf) } |
            ForEach-Object { "$($_.Name) <- $($_.Path)" })
    if ($missing.Count -gt 0) {
        throw ("Missing package inputs:`n  " + ($missing -join "`n  "))
    }
}

function Assert-LoaderHash {
    param([string]$Path, [string]$Expected)
    $actual = (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
    $wanted = $Expected.ToLowerInvariant()
    if ($actual -ne $wanted) {
        throw "Ultimate ASI Loader sha256 mismatch: expected $wanted, got $actual ($Path). Refusing to package an unverified loader."
    }
    return $actual
}

function Expand-LoaderDll {
    param([string]$ZipPath, [string]$Destination)
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::OpenRead($ZipPath)
    try {
        $entry = $archive.Entries |
            Where-Object { $_.Name -eq 'dinput8.dll' -and $_.FullName -notlike '*/__MACOSX/*' } |
            Select-Object -First 1
        if (-not $entry) {
            throw "No dinput8.dll in the Ultimate ASI Loader archive $ZipPath"
        }
        [System.IO.Compression.ZipFileExtensions]::ExtractToFile($entry, $Destination, $true)
    } finally {
        $archive.Dispose()
    }
}

function Write-PackageZip {
    param([object[]]$Plan, [string]$ZipPath)
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    Add-Type -AssemblyName System.IO.Compression
    if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
    # ZIP timestamps cannot predate the DOS epoch (1980-01-01).
    $stamp = [System.DateTimeOffset]::new(1980, 1, 1, 0, 0, 0, [System.TimeSpan]::Zero)
    $archive = [System.IO.Compression.ZipFile]::Open($ZipPath, [System.IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($item in $Plan) {
            $entry = $archive.CreateEntry($item.Name, [System.IO.Compression.CompressionLevel]::Optimal)
            $entry.LastWriteTime = $stamp
            $output = $entry.Open()
            $input = [System.IO.File]::OpenRead($item.Path)
            try {
                $input.CopyTo($output)
            } finally {
                $input.Dispose()
                $output.Dispose()
            }
        }
    } finally {
        $archive.Dispose()
    }
}

# --- version ---------------------------------------------------------------

if (-not (Test-Path -LiteralPath $versionHeader -PathType Leaf)) {
    throw "Cannot read the version: $versionHeader is missing"
}
$headerText = [System.IO.File]::ReadAllText($versionHeader)
$match = [regex]::Match($headerText, 'kBigFixVersion\s*=\s*"([^"]+)"')
if (-not $match.Success) {
    throw "Cannot parse kBigFixVersion from $versionHeader"
}
$version = ($match.Groups[1].Value -replace '[^0-9A-Za-z.\-]', '-').Trim('-')
if (-not $version) { throw "kBigFixVersion in $versionHeader is empty" }

# --- paths -----------------------------------------------------------------

if (-not (Test-Path -LiteralPath $BuildDir -PathType Container)) {
    throw "Build folder not found: $BuildDir (build Release x64 first)"
}
$buildPath = (Resolve-Path -LiteralPath $BuildDir).Path
$outPath = [System.IO.Path]::GetFullPath($OutDir, (Get-Location).Path)
if (-not (Test-Path -LiteralPath $outPath -PathType Container)) {
    New-Item -ItemType Directory -Path $outPath -Force | Out-Null
}
$zipPath = Join-Path $outPath ("BigFix-v{0}.zip" -f $version)

# --- loader ----------------------------------------------------------------

$work = Join-Path ([System.IO.Path]::GetTempPath()) ("bigfix-package-" + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $work | Out-Null
try {
    if ($LoaderZip) {
        if (-not (Test-Path -LiteralPath $LoaderZip -PathType Leaf)) {
            throw "Loader zip not found: $LoaderZip"
        }
        $loaderArchive = (Resolve-Path -LiteralPath $LoaderZip).Path
    } else {
        $loaderArchive = Join-Path $work 'Ultimate-ASI-Loader-NoPDB_x64.zip'
        Write-Host "Downloading $LoaderUrl"
        Invoke-WebRequest -Uri $LoaderUrl -OutFile $loaderArchive -UseBasicParsing
    }
    $loaderHash = Assert-LoaderHash -Path $loaderArchive -Expected $LoaderSha256
    $loaderDll = Join-Path $work 'winmm.dll'
    Expand-LoaderDll -ZipPath $loaderArchive -Destination $loaderDll

    # --- zip ---------------------------------------------------------------

    $plan = Get-PackagePlan -RepoRoot $repoRoot -BuildPath $buildPath -LoaderDll $loaderDll
    if ($IncludeDsoal) {
        . (Join-Path $PSScriptRoot 'package-audio.ps1')
        $plan = @($plan) + @(Get-AudioPackagePlan $repoRoot $work $DsoalZip $DsoalSourceDir $DsoalDataFile)
    } elseif ($DsoalZip -or $DsoalSourceDir -or $DsoalDataFile) {
        throw 'DSOAL input options require -IncludeDsoal.'
    }
    Assert-FilesExist -Plan $plan
    Write-PackageZip -Plan $plan -ZipPath $zipPath

    $zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    Write-Host ("Packaged {0}" -f $zipPath)
    Write-Host ("  version  {0} (loader v10.0.2, sha256 {1})" -f $version, $loaderHash)
    Write-Host ("  contents {0}" -f (($plan | ForEach-Object { $_.Name }) -join ', '))
    Write-Host ("  sha256   {0}" -f $zipHash)
} catch {
    # Never leave a partial or unverified zip behind.
    if (Test-Path -LiteralPath $zipPath) { Remove-Item -LiteralPath $zipPath -Force -ErrorAction SilentlyContinue }
    throw
} finally {
    Remove-Item -LiteralPath $work -Recurse -Force -ErrorAction SilentlyContinue
}

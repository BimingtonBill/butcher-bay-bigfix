# Optional, separately replaceable LGPL DLLs; all network inputs are hash pinned.
function Get-AudioPackagePlan($RepoRoot, $Work, $LocalZip, $SourceDir, $MetadataFile) {
    if ($MetadataFile -and (-not $LocalZip -or -not $SourceDir)) {
        throw '-DsoalDataFile is only allowed with local -DsoalZip and -DsoalSourceDir.'
    }
    if (-not $MetadataFile) { $MetadataFile = Join-Path $RepoRoot 'installer/dsoal-release.json' }
    $release = Get-Content -Raw -LiteralPath $MetadataFile | ConvertFrom-Json
    . (Join-Path $RepoRoot 'installer/audio-payload.ps1')
    $cache = Join-Path $RepoRoot 'local/audio-cache'
    [IO.Directory]::CreateDirectory($cache) | Out-Null
    function Verified-Input($Local, $Url, $Hash) {
        $path = $Local
        if (-not $path) {
            $path = Join-Path $cache ($Hash + '.zip')
            if (-not (Test-Path -LiteralPath $path)) {
                Write-Host "Downloading $Url"
                Invoke-WebRequest -Uri $Url -OutFile $path
            }
        }
        if ((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $Hash) {
            throw "DSOAL source/artifact SHA-256 mismatch: $path. Refusing to package."
        }
        return [IO.Path]::GetFullPath($path)
    }
    function Extract-Entry($Archive, $Name, $Destination) {
        $entries = @($Archive.Entries | Where-Object { $_.FullName -ceq $Name })
        if ($entries.Count -ne 1) { throw "Expected exactly one archive entry: $Name" }
        [IO.Compression.ZipFileExtensions]::ExtractToFile($entries[0], $Destination, $true)
    }
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $outerPath = Verified-Input $LocalZip $release.url $release.sha256
    $outer = [IO.Compression.ZipFile]::OpenRead($outerPath)
    $innerPath = Join-Path $Work 'dsoal-inner.zip'
    try { Extract-Entry $outer $release.nestedArchive $innerPath } finally { $outer.Dispose() }
    $innerPath = Verified-Input $innerPath '' $release.nestedSha256
    $inner = [IO.Compression.ZipFile]::OpenRead($innerPath)
    $audioDir = Join-Path $Work 'audio'
    [IO.Directory]::CreateDirectory($audioDir) | Out-Null
    $plan = [Collections.Generic.List[object]]::new()
    try {
        foreach ($entry in $release.files) {
            $path = Join-Path $audioDir $entry.name
            Extract-Entry $inner ('DSOAL/Win64/' + $entry.name) $path
            $plan.Add([pscustomobject]@{Name=$entry.name; Path=$path})
        }
        Assert-AudioPayload $audioDir $release.files
        foreach ($name in $release.notices) {
            $path = Join-Path $audioDir $name
            Extract-Entry $inner ('DSOAL/Documentation/' + $name) $path
            $plan.Add([pscustomobject]@{Name=('audio/Documentation/'+$name); Path=$path})
        }
    } finally { $inner.Dispose() }
    $links = "# Optional audio dependency sources`n`nArtifact: $($release.url)`nSHA-256: $($release.sha256)`n`n"
    foreach ($source in $release.sources) {
        $localSource = if ($SourceDir) { Join-Path $SourceDir ($source.project+'-source.zip') } else { '' }
        $path = Verified-Input $localSource $source.url $source.sha256
        $plan.Add([pscustomobject]@{Name=('audio/'+$source.project+'-source.zip'); Path=$path})
        $links += "$($source.project): $($source.revision)`n$($source.url)`nSHA-256: $($source.sha256)`n`n"
    }
    $links += "DLLs remain separately replaceable under their LGPL licenses. No HRTF preference is installed.`n"
    $sourceNotice = Join-Path $audioDir 'SOURCES.md'
    [IO.File]::WriteAllText($sourceNotice, $links, [Text.UTF8Encoding]::new($false))
    $plan.Add([pscustomobject]@{Name='audio/SOURCES.md'; Path=$sourceNotice})
    return $plan
}

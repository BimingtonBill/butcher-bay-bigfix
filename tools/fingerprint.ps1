<# Read-only collector; Python 3.9+ is required. Run from outside the game folder. #>
param(
    [Parameter(Mandatory=$true)][string]$GameDir,
    [string]$Output = 'bigfix-fingerprint.json',
    [string]$DataFile
)
$ErrorActionPreference = 'Stop'
$collector = Join-Path $PSScriptRoot 'fingerprint.py'
$arguments = @($collector, '--game-dir', $GameDir, '--output', $Output)
if ($DataFile) { $arguments += @('--data-file', $DataFile) }
foreach ($candidate in @('python3', 'python', 'py')) {
    if (Get-Command $candidate -ErrorAction SilentlyContinue) {
        & $candidate -c 'import sys; sys.exit(sys.version_info < (3,9))' 2>$null
        if ($LASTEXITCODE -eq 0) {
            & $candidate @arguments
            exit $LASTEXITCODE
        }
    }
}
throw 'Fingerprint collection needs Python 3.9 or newer on PATH (python3, python or py).'

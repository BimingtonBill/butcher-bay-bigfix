# Shared by the installer and packager. Parse PE data; never load payload DLLs.
function Assert-AudioPayload([string]$Folder, $Files) {
    foreach ($entry in $Files) {
        $path = Join-Path $Folder $entry.name
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing audio payload: $path" }
        $hash = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($entry.sha256 -notmatch '^[a-fA-F0-9]{64}$' -or $hash -ne $entry.sha256) { throw "Audio SHA-256 mismatch: $path" }
        $bytes = [IO.File]::ReadAllBytes($path)
        function U16([int]$Offset) { [BitConverter]::ToUInt16($bytes, $Offset) }
        function U32([int]$Offset) { [BitConverter]::ToUInt32($bytes, $Offset) }
        try {
            if ((U16 0) -ne 0x5a4d) { throw 'MZ signature' }
            $pe = U32 0x3c
            if ((U32 $pe) -ne 0x4550 -or (U16 ($pe+4)) -ne 0x8664 -or (U16 ($pe+24)) -ne 0x20b) { throw 'AMD64 PE32+ required' }
            $sections = U16 ($pe+6)
            $table = $pe+24+(U16 ($pe+20))
            function Rva([uint32]$Value) {
                for ($i=0; $i -lt $sections; $i++) {
                    $section = $table+40*$i
                    $start = U32 ($section+12)
                    $size = U32 ($section+16)
                    if ($Value -ge $start -and ([uint64]$Value-$start) -lt $size) {
                        $offset = [uint64](U32 ($section+20))+$Value-$start
                        if ($offset -ge $bytes.Length) { throw 'RVA outside file' }
                        return [int]$offset
                    }
                }
                throw 'unmapped RVA'
            }
            $directory = Rva (U32 ($pe+24+112))
            $count = U32 ($directory+24)
            if ($count -gt 65536) { throw 'export count' }
            $names = Rva (U32 ($directory+32))
            $exports = @{}
            for ($i=0; $i -lt $count; $i++) {
                $offset = Rva (U32 ($names+4*$i))
                $end = $offset
                while ($end -lt $bytes.Length -and $bytes[$end] -ne 0 -and $end-$offset -lt 1024) { $end++ }
                if ($end -ge $bytes.Length -or $bytes[$end] -ne 0) { throw 'export name' }
                $exports[[Text.Encoding]::ASCII.GetString($bytes, $offset, $end-$offset)] = $true
            }
            foreach ($name in $entry.exports) {
                if (-not $exports.ContainsKey($name)) { throw "missing export $name" }
            }
        } catch { throw "Invalid audio PE $path ($($_.Exception.Message))" }
    }
}

param([string]$OutputPath = "$PSScriptRoot/../tests/data/modes_rig_2026-10-29.txt")

# DEVMODEW's public display fields; no driver-private bytes are requested.
Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BigFixDisplayDump {
    [StructLayout(LayoutKind.Explicit, Size = 220)]
    public struct Mode {
        [FieldOffset(68)] public ushort Size;
        [FieldOffset(168)] public uint Bpp;
        [FieldOffset(172)] public uint Width;
        [FieldOffset(176)] public uint Height;
        [FieldOffset(184)] public uint Hz;
    }
    [DllImport("user32.dll", CharSet = CharSet.Unicode)]
    private static extern bool EnumDisplaySettingsW(string device, uint index, ref Mode mode);
    // PowerShell binds $null to an empty string for a string parameter. Keep
    // the native null device pointer inside C# to enumerate the primary display.
    public static bool Get(uint index, ref Mode mode) {
        return EnumDisplaySettingsW(null, index, ref mode);
    }
}
'@

$lines = [System.Collections.Generic.List[string]]::new()
for ($index = 0; ; ++$index) {
    $mode = [BigFixDisplayDump+Mode]::new()
    $mode.Size = 220
    if (![BigFixDisplayDump]::Get($index, [ref]$mode)) { break }
    $lines.Add("$($mode.Width) $($mode.Height) $($mode.Bpp) $($mode.Hz)")
}
[System.IO.Directory]::CreateDirectory([System.IO.Path]::GetDirectoryName([System.IO.Path]::GetFullPath($OutputPath))) | Out-Null
[System.IO.File]::WriteAllLines($OutputPath, $lines)
$desktop = [BigFixDisplayDump+Mode]::new()
$desktop.Size = 220
if (![BigFixDisplayDump]::Get([uint32]::MaxValue, [ref]$desktop)) {
    throw 'Cannot read current desktop mode'
}
Write-Output "Captured $($lines.Count) modes; desktop $($desktop.Width)x$($desktop.Height) $($desktop.Bpp)bpp @$($desktop.Hz)Hz"

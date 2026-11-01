<#
.SYNOPSIS
    Themed Windows front end for install.ps1 (Windows PowerShell 5.1 / PowerShell 7).
.DESCRIPTION
    All game detection and changes are delegated to install.ps1. -NoGui prints
    the planned backend arguments as JSON and never shows a window or installs.
    Dot-source to use Get-BigFixSetupArguments with an option-state hashtable.
    -ScreenshotPath renders the form off-screen with DrawToBitmap, without installing.
    -ScreenshotDpi scales that capture (96 = 100%, 144 = 150%); normal windows use monitor DPI.
#>
param(
    [string]$GameDir,
    [string]$SourceDir = $PSScriptRoot,
    [string]$DataFile,
    [string]$RegistryFile,
    [string]$ShortcutDir,
    [string]$DesktopMode,
    [switch]$NoRegistryRepair,
    [switch]$WhatIf,
    [switch]$Uninstall,
    [switch]$NoGui,
    [ValidateSet('system', 'dsoal')][string]$AudioBackend = 'system',
    [switch]$BackupAudioConflicts,
    [ValidateSet('Display', 'Graphics', 'Input', 'Audio', 'Diagnostics')][string]$ScreenshotSection = 'Display',
    [string]$ScreenshotPath,
    [ValidateRange(96, 192)][int]$ScreenshotDpi = 96
)

$ErrorActionPreference = 'Stop'
$script:SetupBackend = Join-Path $PSScriptRoot 'install.ps1'

function Get-BigFixSetupDefaults {
    # The release names this file BigFix.ini; the checkout keeps the template name.
    $path = Join-Path $PSScriptRoot 'BigFix.default.ini'
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { $path = Join-Path $PSScriptRoot 'BigFix.ini' }
    $settings = @{}
    foreach ($line in [IO.File]::ReadAllLines($path)) {
        if ($line -match '^\s*([^;#=\s][^=]*?)\s*=\s*(.*?)\s*$') { $settings[$matches[1]] = $matches[2] }
    }
    return $settings
}

function Get-BigFixSetupArguments([hashtable]$State) {
    $arguments = @{}
    foreach ($name in @('GameDir', 'SourceDir', 'DataFile', 'RegistryFile', 'ShortcutDir', 'DesktopMode')) {
        if (-not [string]::IsNullOrWhiteSpace([string]$State[$name])) { $arguments[$name] = [string]$State[$name] }
    }
    foreach ($name in @('NoRegistryRepair', 'WhatIf', 'Uninstall')) {
        if ($State[$name]) { $arguments[$name] = $true }
    }
    if (-not $State.Uninstall) {
        $arguments.AudioBackend = if ($State.AudioBackend) { [string]$State.AudioBackend } else { 'system' }
        if ($State.BackupAudioConflicts) { $arguments.BackupAudioConflicts = $true }
        $arguments.ShaderMode = if ($State.ContainsKey('ShaderMode')) { [int]$State.ShaderMode } else { 5 }
        $arguments.IniOverrides = if ($State.ContainsKey('IniOverrides')) { $State.IniOverrides } else {
            $defaults = Get-BigFixSetupDefaults
            $selected = @{}
            foreach ($name in @('RawMouse', 'TextureSizeFix', 'FixMiscBugs', 'Enable20PlusPlusAnyGpu',
                'EyeshineFix', 'EyeshineFlickerWorkaround', 'DisplayModeFilter', 'HudAspectFix',
                'VideoAspectFix', 'SkipLogoVideos', 'BackgroundAudio', 'CapturePng', 'TraceEax',
                'FOV', 'FrameCap', 'AntiAliasing', 'WindowMode')) { $selected[$name] = $defaults[$name] }
            $selected
        }
    }
    return $arguments
}

function New-BigFixBackendProcess([hashtable]$Arguments) {
    # Encode data separately from code: spaces, Unicode, apostrophes and shell
    # metacharacters in paths cannot become PowerShell or cmd instructions.
    $json = ConvertTo-Json -InputObject $Arguments -Depth 6 -Compress
    $data = [Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes($json))
    $backend = $script:SetupBackend.Replace("'", "''")
    $command = @"
`$ErrorActionPreference = 'Stop'
`$ProgressPreference = 'SilentlyContinue'
[Console]::OutputEncoding = [Text.UTF8Encoding]::new(`$false)
function Convert-Map(`$value) {
    if (`$value -is [Management.Automation.PSCustomObject]) {
        `$map = @{}
        foreach (`$property in `$value.PSObject.Properties) { `$map[`$property.Name] = Convert-Map `$property.Value }
        return `$map
    }
    return `$value
}
`$argsMap = Convert-Map (ConvertFrom-Json ([Text.Encoding]::UTF8.GetString([Convert]::FromBase64String('$data'))))
try { & '$backend' @argsMap }
catch { [Console]::Error.WriteLine(`$_.Exception.Message); exit 1 }
"@
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = (Get-Process -Id $PID).Path
    $start.Arguments = '-NoProfile -NonInteractive -OutputFormat Text -ExecutionPolicy Bypass -EncodedCommand ' +
        [Convert]::ToBase64String([Text.Encoding]::Unicode.GetBytes($command))
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $start.StandardOutputEncoding = [Text.Encoding]::UTF8
    $start.StandardErrorEncoding = [Text.Encoding]::UTF8
    $process = New-Object Diagnostics.Process
    $process.StartInfo = $start
    [void]$process.Start()
    return $process
}

function Show-BigFixSetup([hashtable]$InitialState, [string]$CapturePath,
    [ValidateRange(96, 192)][int]$CaptureDpi = 96,
    [ValidateSet('Display', 'Graphics', 'Input', 'Audio', 'Diagnostics')][string]$CaptureSection = 'Display') {
    Add-Type -AssemblyName System.Windows.Forms
    Add-Type -AssemblyName System.Drawing
    # PS5.1 uses .NET Framework; PS7 exposes SetHighDpiMode. Set awareness before
    # creating any controls, with a native fallback for Framework hosts.
    if ([Windows.Forms.Application].GetMethod('SetHighDpiMode')) {
        [void][Windows.Forms.Application]::SetHighDpiMode([Windows.Forms.HighDpiMode]::PerMonitorV2)
    } else {
        Add-Type @'
using System;
using System.Runtime.InteropServices;
public static class BigFixDpi {
    [DllImport("user32.dll")] public static extern bool SetProcessDpiAwarenessContext(IntPtr value);
    [DllImport("user32.dll")] public static extern bool SetProcessDPIAware();
    public static void Enable() {
        try { SetProcessDpiAwarenessContext(new IntPtr(-4)); }
        catch (EntryPointNotFoundException) { SetProcessDPIAware(); }
    }
}
'@
        [BigFixDpi]::Enable()
    }
    [Windows.Forms.Application]::EnableVisualStyles()
    $coal = [Drawing.ColorTranslator]::FromHtml('#111619')
    $steel = [Drawing.ColorTranslator]::FromHtml('#1A2023')
    $bone = [Drawing.ColorTranslator]::FromHtml('#E7E4DD')
    $amber = [Drawing.ColorTranslator]::FromHtml('#FFB43E')
    $muted = [Drawing.ColorTranslator]::FromHtml('#B5BEC7')
    $edge = [Drawing.ColorTranslator]::FromHtml('#667078')
    $ui = @{ Process = $null; Action = ''; Info = $null; State = $InitialState; Checks = @{};
        Pages = @{}; Sections = @{}; LogExpanded = $false; LastResult = ''; LayoutScale = 1;
        LogPath = Join-Path ([IO.Path]::GetTempPath()) ('BigFixSetup-{0:yyyyMMdd-HHmmss}-{1}.log' -f (Get-Date), $PID) }
    $form = New-Object Windows.Forms.Form
    $form.Text = 'Butcher Bay BigFix | Setup'
    $form.ClientSize = New-Object Drawing.Size(1000, 880)
    $form.MinimumSize = New-Object Drawing.Size(700, 500)
    $form.StartPosition = 'CenterScreen'
    $form.AutoScaleDimensions = New-Object Drawing.SizeF(96, 96)
    $form.AutoScaleMode = 'Dpi'
    $form.BackColor = $coal
    $form.ForeColor = $bone
    $form.Font = New-Object Drawing.Font('Segoe UI', 10)
    $form.AutoScroll = $true
    # Keep the whole layout reachable on small displays and at high DPI.
    # WinForms scales this logical extent along with the controls.
    $form.AutoScrollMinSize = New-Object Drawing.Size(1000, 880)

    # Bahnschrift is a variable font; GDI does not consistently expose its named
    # condensed instances. Prefer installed condensed families, then a safe UI face.
    $displayFace = 'Segoe UI'
    foreach ($face in @('Bahnschrift SemiBold Condensed', 'Bahnschrift Condensed', 'Impact', 'Arial Narrow')) {
        $candidate = New-Object Drawing.Font($face, 12, [Drawing.FontStyle]::Bold)
        try { if ($candidate.FontFamily.Name -eq $face) { $displayFace = $face; break } }
        finally { $candidate.Dispose() }
    }

    function Add-SetupLabel($Parent, [string]$Text) {
        $label = New-Object Windows.Forms.Label
        $label.Text = $Text
        $label.AutoSize = $true
        $label.Margin = New-Object Windows.Forms.Padding(0, 5, 12, 5)
        $label.UseMnemonic = $true
        $Parent.Controls.Add($label)
        return $label
    }
    function New-SetupButton([string]$Text, [int]$Index) {
        $button = New-Object Windows.Forms.Button
        $button.Text = $Text
        $button.AutoSize = $true
        $button.MinimumSize = New-Object Drawing.Size(110, 36)
        $button.FlatStyle = 'Flat'
        $button.FlatAppearance.BorderColor = $edge
        $button.FlatAppearance.MouseOverBackColor = [Drawing.Color]::FromArgb(57, 63, 67)
        $button.FlatAppearance.MouseDownBackColor = [Drawing.Color]::FromArgb(70, 76, 80)
        $button.BackColor = $steel
        $button.ForeColor = $bone
        $button.TabIndex = $Index
        $button.Margin = New-Object Windows.Forms.Padding(0, 0, 12, 0)
        return $button
    }
    function New-SetupText([string]$Value, [int]$Width) {
        $text = New-Object Windows.Forms.TextBox
        $text.Text = $Value
        $text.Width = $Width
        $text.BackColor = $steel
        $text.ForeColor = $bone
        $text.BorderStyle = 'FixedSingle'
        return $text
    }
    function New-SetupChoice($Items, [int]$Width) {
        $choice = New-Object Windows.Forms.ComboBox
        $choice.DropDownStyle = 'DropDownList'
        $choice.Width = $Width
        $choice.BackColor = $steel
        $choice.ForeColor = $bone
        $choice.FlatStyle = 'Flat'
        $choice.DrawMode = 'OwnerDrawFixed'
        $choice.ItemHeight = $choice.Font.Height + 6
        $choice.Add_FontChanged({ param($sender, $event) $sender.ItemHeight = $sender.Font.Height + 6 })
        $choice.Add_DrawItem({
            param($sender, $event)
            $background = if (($event.State -band [Windows.Forms.DrawItemState]::Selected) -ne 0) { $amber } else { $steel }
            $foreground = if (($event.State -band [Windows.Forms.DrawItemState]::Selected) -ne 0) { $coal } else { $bone }
            $brush = New-Object Drawing.SolidBrush($background)
            try {
                $event.Graphics.FillRectangle($brush, $event.Bounds)
                if ($event.Index -ge 0) {
                    [Windows.Forms.TextRenderer]::DrawText($event.Graphics, [string]$sender.Items[$event.Index],
                        $sender.Font, $event.Bounds, $foreground,
                        [Windows.Forms.TextFormatFlags]'Left,VerticalCenter,NoPrefix')
                }
                $event.DrawFocusRectangle()
            } finally { $brush.Dispose() }
        })
        foreach ($item in $Items) { [void]$choice.Items.Add([string]$item) }
        $choice.SelectedIndex = 0
        return $choice
    }
    function Write-SetupLog([string]$Text) {
        $ui.Log.AppendText($Text + [Environment]::NewLine)
        $ui.Log.SelectionStart = $ui.Log.TextLength
        $ui.Log.ScrollToCaret()
        [IO.File]::AppendAllText($ui.LogPath, $Text + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
    }
    function Show-SetupError([string]$Text) {
        [void][Windows.Forms.MessageBox]::Show($form, $Text, 'BigFix setup', 'OK', 'Error')
    }
    function Set-SetupDetectionFailure([string]$Details) {
        $ui.Info = $null
        $ui.BuildStatus.Text = 'Game not detected'
        $ui.BuildIcon.Tag = 'warning'
        $ui.BuildIcon.AccessibleName = 'Game not found'
        $ui.BuildIcon.Invalidate()
        $ui.Status.Text = 'Browse to its folder, then choose Detect.'
        $ui.ActivityTitle.Text = 'Game not found'
        $ui.Activity.Text = 'No changes made. Browse to the game folder.'
        Write-SetupLog $Details.Trim()
    }
    function Update-SetupActions {
        $idle = $null -eq $ui.Process
        $ui.FolderRow.Enabled = $idle
        $ui.Options.Enabled = $idle
        $valid = $null -ne $ui.Info
        $ui.Install.Enabled = $idle -and $valid -and $ui.Info.Status -eq 'known'
        $ui.Install.Text = if ($valid -and $ui.Info.Installed) { 'RE&INSTALL  ›' } else { '&INSTALL  ›' }
        $ui.Uninstall.Enabled = $idle -and $valid -and $ui.Info.Installed
        $ui.Launch.Enabled = $idle -and $valid
        $ui.Close.Enabled = $idle
        $ui.OpenLog.Enabled = Test-Path -LiteralPath $ui.LogPath
        $ui.Progress.AccessibleDescription = if ($idle) { 'Idle' } else { 'Operation running; progress is indeterminate' }
        $ui.Progress.Invalidate()
    }
    function Set-SetupSettings($Settings) {
        foreach ($name in $ui.Checks.Keys) {
            if ($null -ne $Settings[$name]) { $ui.Checks[$name].Checked = [string]$Settings[$name] -eq '1' }
        }
        foreach ($name in @('FOV', 'FrameCap')) {
            if ($null -ne $Settings[$name]) {
                $value = 0D
                if ([decimal]::TryParse([string]$Settings[$name], [Globalization.NumberStyles]::Float,
                    [Globalization.CultureInfo]::InvariantCulture, [ref]$value)) {
                    $ui[$name].Value = [Math]::Max($ui[$name].Minimum, [Math]::Min($ui[$name].Maximum, [Math]::Round($value)))
                }
            }
        }
        foreach ($name in @('AntiAliasing', 'WindowMode')) {
            if ($null -ne $Settings[$name] -and $ui[$name].Items.Contains([string]$Settings[$name])) {
                $ui[$name].SelectedItem = [string]$Settings[$name]
            }
        }
    }
    function Set-SetupInspection([string]$Json) {
        $info = ConvertFrom-Json $Json
        $ui.Folder.Text = $info.GameDir
        $ui.Info = $info
        $ui.Dsoal.Enabled = [bool]$info.AudioPayloadAvailable
        $ui.AudioAvailability.Text = if ($info.AudioPayloadAvailable) {
            'DSOAL restores EAX reverb. Switching backend requires uninstalling, then reinstalling.'
        } else {
            'DSOAL unavailable: dsound.dll and dsoal-aldrv.dll are required in the package folder.'
        }
        $ui.AudioTooltip.SetToolTip($ui.Dsoal, $ui.AudioAvailability.Text)
        $ui.AudioInstalled.Text = if ($info.Installed) {
            'Installed backend: ' + $(if ($info.AudioBackend -eq 'dsoal') { 'DSOAL + OpenAL Soft' } else { 'Windows audio' }) + "`r`nSwitching backend: uninstall, then reinstall."
        } else { 'Switching backend requires uninstalling, then reinstalling.' }
        $ui.BuildStatus.Text = if ($info.BuildLabel) { "$($info.BuildLabel) detected" } else { 'Unknown game build' }
        $readiness = switch ($info.Status) {
            'known' { if ($info.Installed) { 'Installed — reinstall to update settings.' } else { 'Ready to install.' } }
            'needs 1.1 patch' { 'Apply the official 1.1 patch before installing.' }
            default { 'Build verification needed. See the activity log.' }
        }
        $ui.Status.Text = $readiness
        $ui.BuildIcon.Tag = if ($info.Status -eq 'known') { 'check' } else { 'warning' }
        $ui.BuildIcon.AccessibleName = if ($info.Status -eq 'known') { 'Verified build' } else { 'Build needs attention' }
        $ui.BuildIcon.Invalidate()
        $ui.ActivityTitle.Text = if ($info.Status -eq 'known') { 'Ready' } else { 'Needs attention' }
        $ui.Activity.Text = if ($ui.LastResult) { $ui.LastResult } else { 'No changes made.' }
        if ($info.NeedsRegistryRepair) {
            Write-SetupLog 'Optional registry path repair is available; setup asks for consent before changes.'
        }
        if ($info.DesktopMode -and -not $ui.Desktop.Items.Contains([string]$info.DesktopMode)) {
            [void]$ui.Desktop.Items.Add([string]$info.DesktopMode)
        }
        # Load settings on folder changes. A post-preview refresh must retain the
        # selections the user is about to install, rather than reverting to disk.
        if ($ui.LoadedFolder -ne $info.GameDir) {
            Set-SetupSettings (Get-BigFixSetupDefaults)
            $settings = @{}
            foreach ($property in $info.Settings.PSObject.Properties) { $settings[$property.Name] = $property.Value }
            Set-SetupSettings $settings
            $ui.Dsoal.Checked = $info.AudioBackend -eq 'dsoal'
            $ui.SystemAudio.Checked = -not $ui.Dsoal.Checked
            $ui.LoadedFolder = $info.GameDir
        }
        Write-SetupLog $ui.Status.Text
    }
    function Start-SetupAction([string]$Action, [hashtable]$Arguments) {
        if ($ui.Process) { return }
        $ui.Action = $Action
        $ui.ActivityTitle.Text = if ($Action -eq 'Inspect') { 'Checking' } else { $Action }
        $ui.BuildStatus.Text = if ($Action -eq 'Inspect') { 'Checking installation' } else { "$Action in progress" }
        $ui.Output = New-Object Text.StringBuilder
        $ui.Errors = New-Object Text.StringBuilder
        $ui.Process = New-BigFixBackendProcess $Arguments
        $ui.ReadOut = $ui.Process.StandardOutput.ReadLineAsync()
        $ui.ReadErr = $ui.Process.StandardError.ReadLineAsync()
        $ui.Status.Text = if ($Action -eq 'Inspect') { 'Detecting game build...' } else { "$Action in progress..." }
        $ui.Activity.Text = $ui.Status.Text
        Update-SetupActions
        $ui.Timer.Start()
    }
    function Request-SetupInspection {
        $ui.Info = $null
        $argsMap = @{} + $ui.State
        $argsMap.GameDir = $ui.Folder.Text.Trim()
        $argsMap = Get-BigFixSetupArguments $argsMap
        $argsMap.Remove('IniOverrides')
        $argsMap.Remove('ShaderMode')
        $argsMap.Remove('Uninstall')
        $argsMap.Inspect = $true
        Start-SetupAction 'Inspect' $argsMap
    }
    function Invoke-SetupInstall([bool]$Remove) {
        try {
            if (-not $ui.Info -or $ui.Folder.Text.Trim() -ne $ui.Info.GameDir) {
                Request-SetupInspection
                return
            }
            $state = @{} + $ui.State
            $state.GameDir = $ui.Info.GameDir
            $state.Uninstall = $Remove
            $state.WhatIf = $ui.Preview.Checked
            $state.NoRegistryRepair = -not $ui.Repair.Checked
            $state.DesktopMode = if ($ui.Desktop.Text -eq 'Automatic (recommended)') { '' } else { $ui.Desktop.Text.Trim() }
            $state.ShaderMode = if ($ui.Shader.SelectedIndex -eq 1) { 6 } else { 5 }
            $settings = @{}
            foreach ($key in $ui.Checks.Keys) { $settings[$key] = if ($ui.Checks[$key].Checked) { '1' } else { '0' } }
            foreach ($key in @('FOV', 'FrameCap')) {
                $settings[$key] = $ui[$key].Value.ToString('0', [Globalization.CultureInfo]::InvariantCulture)
            }
            foreach ($key in @('AntiAliasing', 'WindowMode')) { $settings[$key] = [string]$ui[$key].SelectedItem }
            $state.IniOverrides = $settings
            $state.AudioBackend = if ($ui.Dsoal.Checked) { 'dsoal' } else { 'system' }
            $state.BackupAudioConflicts = $false
            if (-not $Remove -and $ui.Info.Installed -and
                ($state.AudioBackend -eq 'dsoal' -or $ui.Info.AudioBackend -eq 'dsoal')) {
                Show-SetupError 'Uninstall the current BigFix installation first, then install with your chosen audio backend.'
                return
            }
            if (-not $Remove -and $state.AudioBackend -eq 'dsoal' -and @($ui.Info.AudioConflicts).Count) {
                $answer = [Windows.Forms.MessageBox]::Show($form,
                    ('Existing audio files: ' + ($ui.Info.AudioConflicts -join ', ') +
                    '. Back up these third-party files reversibly and install DSOAL? Uninstall restores the backups.'),
                    'Audio backup consent', 'YesNo', 'Question', 'Button2')
                if ($answer -ne 'Yes') { return }
                $state.BackupAudioConflicts = $true
            }
            if (-not $Remove -and -not $state.WhatIf -and -not $state.NoRegistryRepair -and $ui.Info.NeedsRegistryRepair) {
                $answer = [Windows.Forms.MessageBox]::Show($form,
                    'Repair the truncated registry install path? This lets the official patch find the game. Windows may also ask for administrator permission. Choose No to install without repair.',
                    'Registry repair consent', 'YesNoCancel', 'Question')
                if ($answer -eq 'Cancel') { return }
                if ($answer -eq 'No') { $state.NoRegistryRepair = $true }
            }
            $action = if ($Remove) { 'Uninstall' } else { 'Install' }
            Write-SetupLog ("$action requested" + $(if ($state.WhatIf) { ' (preview only)' } else { '' }))
            Start-SetupAction $action (Get-BigFixSetupArguments $state)
        } catch { Show-SetupError $_.Exception.Message }
    }
    # Presentation only: the existing option-state keys still feed install.ps1.
    $root = New-Object Windows.Forms.TableLayoutPanel
    $root.Dock = 'Fill'
    $root.MinimumSize = New-Object Drawing.Size(950, 860)
    $root.Padding = New-Object Windows.Forms.Padding(24, 16, 24, 16)
    $root.ColumnCount = 1
    $root.RowCount = 7
    foreach ($height in @(126, 194, 0, 78, 40, 0, 28)) {
        $style = if ($height -eq 0 -and $root.RowStyles.Count -eq 2) {
            New-Object Windows.Forms.RowStyle('Percent', 100)
        } else { New-Object Windows.Forms.RowStyle('Absolute', $height) }
        [void]$root.RowStyles.Add($style)
    }
    $form.Controls.Add($root)

    function New-SetupCard {
        $card = New-Object Windows.Forms.TableLayoutPanel
        $card.Dock = 'Fill'
        $card.BackColor = $steel
        $card.Padding = New-Object Windows.Forms.Padding(16, 12, 16, 12)
        $card.Margin = New-Object Windows.Forms.Padding(0, 0, 0, 14)
        $card.Add_Paint({
            param($sender, $event)
            $pen = New-Object Drawing.Pen($edge)
            try { $event.Graphics.DrawRectangle($pen, 0, 0, ($sender.Width - 1), ($sender.Height - 1)) }
            finally { $pen.Dispose() }
        })
        return $card
    }
    function Add-SetupHeading($Parent, [string]$Text) {
        $heading = Add-SetupLabel $Parent $Text.ToUpperInvariant()
        $heading.Font = New-Object Drawing.Font($displayFace, 19, [Drawing.FontStyle]::Bold)
        $heading.Margin = New-Object Windows.Forms.Padding(0)
        return $heading
    }
    function New-SetupCheck([string]$Text) {
        $check = New-Object Windows.Forms.CheckBox
        $check.Text = $Text
        $check.AccessibleName = $Text.Replace('&', '')
        $check.AutoSize = $false
        $check.Height = 27
        $check.FlatStyle = 'Flat'
        $check.Dock = 'Fill'
        $check.Margin = New-Object Windows.Forms.Padding(0)
        # Keep the native checkbox's role, Space/Alt handling and focus behavior.
        # Only paint its surface, including mnemonic and keyboard focus cues.
        $check.Add_Paint({
            param($sender, $event)
            $g = $event.Graphics
            $scale = $sender.Height / 27.0
            $size = [int](18 * $scale)
            $x = [int](3 * $scale); $y = [int](($sender.Height - $size) / 2)
            $background = New-Object Drawing.SolidBrush($sender.BackColor)
            $fill = New-Object Drawing.SolidBrush($amber)
            $border = New-Object Drawing.Pen($muted, [single]$scale)
            $tick = New-Object Drawing.Pen($coal, [single](2.3 * $scale))
            try {
                $g.FillRectangle($background, $sender.ClientRectangle)
                if ($sender.Checked) {
                    $g.FillRectangle($fill, $x, $y, $size, $size)
                    $g.DrawLines($tick, [Drawing.PointF[]]@(
                        (New-Object Drawing.PointF(($x + 4 * $scale), ($y + 9 * $scale))),
                        (New-Object Drawing.PointF(($x + 8 * $scale), ($y + 13 * $scale))),
                        (New-Object Drawing.PointF(($x + 14 * $scale), ($y + 5 * $scale)))))
                } else { $g.DrawRectangle($border, $x, $y, $size, $size) }
                $bounds = New-Object Drawing.Rectangle(([int](30 * $scale)), 0,
                    ([Math]::Max(1, $sender.Width - [int](30 * $scale))), $sender.Height)
                $flags = [Windows.Forms.TextFormatFlags]'Left,VerticalCenter,SingleLine'
                $color = if ($sender.Enabled) { $bone } else { $muted }
                [Windows.Forms.TextRenderer]::DrawText($g, $sender.Text, $sender.Font, $bounds, $color, $flags)
                if ($sender.Focused) {
                    [Windows.Forms.ControlPaint]::DrawFocusRectangle($g, $sender.ClientRectangle, $bone, $sender.BackColor)
                }
            } finally { $background.Dispose(); $fill.Dispose(); $border.Dispose(); $tick.Dispose() }
        })
        return $check
    }
    function Add-SetupCheck($Page, [string]$Key, [string]$Text, [int]$Column, [int]$Row, [string]$Description) {
        $group = New-Object Windows.Forms.TableLayoutPanel
        $group.Dock = 'Fill'
        $group.ColumnCount = 1
        $group.RowCount = 2
        $group.Margin = New-Object Windows.Forms.Padding(16, 0, 4, 0)
        [void]$group.RowStyles.Add((New-Object Windows.Forms.RowStyle('Absolute', 27)))
        [void]$group.RowStyles.Add((New-Object Windows.Forms.RowStyle('Percent', 100)))
        $group.TabIndex = $Row * 4 + $Column
        $check = New-SetupCheck $Text
        $check.AccessibleDescription = $Description
        $check.TabIndex = 0
        $group.Controls.Add($check, 0, 0)
        $note = Add-SetupLabel $group $Description
        $note.Font = New-Object Drawing.Font('Segoe UI', 9)
        $note.ForeColor = $muted
        $note.Margin = New-Object Windows.Forms.Padding(30, 0, 0, 0)
        $note.UseMnemonic = $false
        $Page.Controls.Add($group, $Column, $Row)
        $Page.SetColumnSpan($group, 2)
        if ($Key) { $ui.Checks[$Key] = $check }
        return $check
    }
    function Add-SetupField($Page, [string]$Text, $Control, [int]$Column, [int]$Row, [string]$Hint = '') {
        # Label precedes its input in the same container for Alt-key navigation.
        $label = Add-SetupLabel $Page $Text
        $label.TabIndex = $Row * 4 + $Column
        $Page.SetCellPosition($label, (New-Object Windows.Forms.TableLayoutPanelCellPosition($Column, $Row)))
        $Control.TabIndex = $label.TabIndex + 1
        $label.Anchor = 'Left'
        $label.Margin = New-Object Windows.Forms.Padding(6, 0, 0, 0)
        $Control.Anchor = 'Left,Right'
        $Control.Margin = New-Object Windows.Forms.Padding(0, 0, 16, 0)
        if ($Hint) {
            $rowPanel = New-Object Windows.Forms.TableLayoutPanel
            $rowPanel.ColumnCount = 2
            $rowPanel.Dock = 'Fill'
            $rowPanel.Margin = New-Object Windows.Forms.Padding(0)
            [void]$rowPanel.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Absolute', 94)))
            [void]$rowPanel.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Percent', 100)))
            $rowPanel.Controls.Add($Control, 0, 0)
            $Control.Margin = New-Object Windows.Forms.Padding(0, 0, 8, 0)
            $hintLabel = Add-SetupLabel $rowPanel $Hint
            $hintLabel.ForeColor = $muted
            $hintLabel.Font = New-Object Drawing.Font('Segoe UI', 9)
            $hintLabel.Anchor = 'Left'
            $hintLabel.Margin = New-Object Windows.Forms.Padding(0)
            $Page.Controls.Add($rowPanel, ($Column + 1), $Row)
            $rowPanel.TabIndex = $Control.TabIndex
        } else { $Page.Controls.Add($Control, ($Column + 1), $Row) }
    }
    function New-SetupNumber([decimal]$Maximum) {
        $number = New-Object Windows.Forms.NumericUpDown
        $number.Minimum = 0
        $number.Maximum = $Maximum
        $number.DecimalPlaces = 0
        $number.BackColor = $steel
        $number.ForeColor = $bone
        $number.BorderStyle = 'FixedSingle'
        return $number
    }
    function Add-SetupNote($Page, [string]$Text, [int]$Row, [int]$Column = 0, [int]$Span = 4) {
        $note = Add-SetupLabel $Page $Text
        $note.AutoSize = $false
        $note.Dock = 'Fill'
        $note.ForeColor = $muted
        $Page.SetCellPosition($note, (New-Object Windows.Forms.TableLayoutPanelCellPosition($Column, $Row)))
        $Page.SetColumnSpan($note, $Span)
        return $note
    }
    function Set-SetupSection([string]$Name) {
        foreach ($key in $ui.Pages.Keys) {
            $selected = $key -eq $Name
            $ui.Pages[$key].Visible = $selected
            $ui.Sections[$key].BackColor = if ($selected) { $amber } else { $coal }
            $ui.Sections[$key].ForeColor = if ($selected) { $coal } else { $bone }
            $ui.Sections[$key].FlatAppearance.BorderColor = if ($selected) { $amber } else { $edge }
            $ui.Sections[$key].FlatAppearance.MouseOverBackColor = if ($selected) {
                [Drawing.Color]::FromArgb(255, 203, 136)
            } else { [Drawing.Color]::FromArgb(57, 63, 67) }
            $ui.Sections[$key].FlatAppearance.MouseDownBackColor = if ($selected) {
                [Drawing.Color]::FromArgb(235, 160, 76)
            } else { [Drawing.Color]::FromArgb(70, 76, 80) }
            $ui.Sections[$key].AccessibleDescription = if ($selected) { 'Selected section' } else { 'Show this section' }
        }
    }
    function Set-SetupLogExpanded([bool]$Expanded) {
        $ui.LogExpanded = $Expanded
        $ui.Log.Visible = $Expanded
        $ui.LogToggle.Text = if ($Expanded) { '▾  Hide activity &log' } else { '▸  Show activity &log' }
        $ui.LogToggle.AccessibleDescription = if ($Expanded) { 'Expanded' } else { 'Collapsed' }
        $extra = if ($Expanded) { 200 * $ui.LayoutScale } else { 0 }
        $root.RowStyles[5].Height = $extra
        $root.MinimumSize = New-Object Drawing.Size(([int](950 * $ui.LayoutScale)), ([int](860 * $ui.LayoutScale + $extra)))
        $form.AutoScrollMinSize = New-Object Drawing.Size(([int](1000 * $ui.LayoutScale)), ([int](880 * $ui.LayoutScale + $extra)))
    }

    $header = New-Object Windows.Forms.Panel
    $header.Dock = 'Fill'
    $header.Margin = New-Object Windows.Forms.Padding(0, 0, 0, 14)
    $header.AccessibleName = 'BigFix — Butcher Bay Community Patch'
    $header.Add_Paint({
        param($sender, $event)
        $g = $event.Graphics
        $rect = $sender.ClientRectangle
        if ($rect.Width -le 0 -or $rect.Height -le 0) { return }
        $scale = $rect.Height / 112.0
        $saved = $g.Save()
        $g.ScaleTransform([single]$scale, [single]$scale)
        $w = [single]($rect.Width / $scale); $h = 112
        $g.SmoothingMode = 'AntiAlias'
        $g.TextRenderingHint = 'AntiAliasGridFit'
        $brush = New-Object Drawing.Drawing2D.LinearGradientBrush(
            (New-Object Drawing.Rectangle(0, 0, ([int]$w), $h)),
            [Drawing.Color]::FromArgb(40, 45, 46), [Drawing.Color]::FromArgb(13, 18, 21), 90.0)
        $paint = New-Object Drawing.SolidBrush($amber)
        $ink = New-Object Drawing.SolidBrush($bone)
        $dark = New-Object Drawing.SolidBrush([Drawing.Color]::FromArgb(14, 18, 20))
        $scratch = New-Object Drawing.Pen([Drawing.Color]::FromArgb(22, 194, 199, 195))
        $edgePen = New-Object Drawing.Pen($edge)
        $titlePath = New-Object Drawing.Drawing2D.GraphicsPath
        $font = New-Object Drawing.Font($displayFace, ([single](64.0 / 3)),
            [Drawing.FontStyle]::Bold, [Drawing.GraphicsUnit]::Pixel)
        $matrix = New-Object Drawing.Drawing2D.Matrix
        try {
            $g.FillRectangle($brush, [single]0, [single]0, $w, [single]$h)
            # Broad painted warning bars; wear is drawn over the paint as well.
            $clip = $g.Save()
            $g.SetClip((New-Object Drawing.RectangleF(($w - 270), 1, 269, 110)))
            for ($x = $w - 330; $x -lt $w + 80; $x += 88) {
                $g.FillPolygon($paint, [Drawing.PointF[]]@(
                    (New-Object Drawing.PointF($x, 112)), (New-Object Drawing.PointF(($x + 44), 112)),
                    (New-Object Drawing.PointF(($x + 130), 0)), (New-Object Drawing.PointF(($x + 86), 0))))
            }
            $g.Restore($clip)
            $grain = New-Object Random(2004)
            for ($i = 0; $i -lt 2400; $i++) {
                $x = [single]($grain.NextDouble() * $w); $y = [single]($grain.NextDouble() * $h)
                $g.FillRectangle($dark, $x, $y, [single](1 + $grain.NextDouble() * 3), [single](0.4 + $grain.NextDouble() * 1.5))
            }
            for ($i = 0; $i -lt 260; $i++) {
                $x = [single]($grain.NextDouble() * $w); $y = [single]($grain.NextDouble() * $h)
                $g.DrawLine($scratch, $x, $y, [single]($x + 2 + $grain.NextDouble() * 24), [single]($y - 1))
            }
            # Draw the wordmark as vector outlines, with deterministic condensed
            # proportions even when GDI falls back from Bahnschrift's named faces.
            $titlePath.AddString('BIGFIX', $font.FontFamily, [int][Drawing.FontStyle]::Bold,
                80, (New-Object Drawing.PointF(0, 0)), [Drawing.StringFormat]::GenericTypographic)
            $bounds = $titlePath.GetBounds()
            $matrix.Translate(-$bounds.X, -$bounds.Y)
            $titlePath.Transform($matrix)
            $matrix.Reset(); $matrix.Scale((305 / $bounds.Width), (76 / $bounds.Height))
            $titlePath.Transform($matrix)
            $matrix.Reset(); $matrix.Translate(28, 18); $titlePath.Transform($matrix)
            $g.FillPath($ink, $titlePath)
            $g.DrawLine($edgePen, [single]352, [single]28, [single]352, [single]87)
            foreach ($line in @(@('BUTCHER BAY', 31), @('COMMUNITY PATCH', 60))) {
                $x = [single]370
                foreach ($character in $line[0].ToCharArray()) {
                    $g.DrawString([string]$character, $font, $ink, $x, [single]$line[1], [Drawing.StringFormat]::GenericTypographic)
                    $x += $g.MeasureString([string]$character, $font, 100, [Drawing.StringFormat]::GenericTypographic).Width + 3.0
                }
            }
            $g.DrawRectangle($edgePen, 0, 0, ($w - 1), 111)
            foreach ($x in @(11, ($w - 12))) {
                foreach ($y in @(11, 100)) {
                    $g.FillEllipse($dark, [single]($x - 6), [single]($y - 6), [single]12, [single]12)
                    $g.DrawArc($edgePen, [single]($x - 5), [single]($y - 5), [single]9, [single]9, [single]190, [single]170)
                    $g.DrawLine($scratch, [single]($x - 2), [single]($y + 2), [single]($x + 2), [single]($y - 2))
                }
            }
        } finally {
            $brush.Dispose(); $paint.Dispose(); $ink.Dispose(); $dark.Dispose(); $scratch.Dispose()
            $edgePen.Dispose(); $titlePath.Dispose(); $font.Dispose(); $matrix.Dispose(); $g.Restore($saved)
        }
    })
    $root.Controls.Add($header, 0, 0)

    $installation = New-SetupCard
    $installation.ColumnCount = 2
    [void]$installation.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Percent', 100)))
    [void]$installation.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Absolute', 234)))
    $installation.TabIndex = 0
    $root.Controls.Add($installation, 0, 1)
    $game = New-Object Windows.Forms.TableLayoutPanel
    $game.Dock = 'Fill'
    $game.ColumnCount = 1
    $game.RowCount = 4
    $game.Margin = New-Object Windows.Forms.Padding(0, 0, 18, 0)
    foreach ($height in @(32, 26, 40, 56)) { [void]$game.RowStyles.Add((New-Object Windows.Forms.RowStyle('Absolute', $height))) }
    [void](Add-SetupHeading $game 'Game installation')
    $gameIntro = Add-SetupLabel $game 'Select the folder where Escape from Butcher Bay is installed.'
    $gameIntro.Margin = New-Object Windows.Forms.Padding(0)
    $installation.Controls.Add($game, 0, 0)
    $folderRow = New-Object Windows.Forms.TableLayoutPanel
    $folderRow.Dock = 'Fill'
    $folderRow.ColumnCount = 3
    $folderRow.Margin = New-Object Windows.Forms.Padding(0)
    foreach ($width in @(0, 100, 90)) {
        $style = if ($width) { New-Object Windows.Forms.ColumnStyle('Absolute', $width) } else { New-Object Windows.Forms.ColumnStyle('Percent', 100) }
        [void]$folderRow.ColumnStyles.Add($style)
    }
    $folderRow.TabIndex = 0
    $ui.Folder = New-SetupText $InitialState.GameDir 400
    $ui.Folder.Dock = 'Fill'
    $ui.Folder.Margin = New-Object Windows.Forms.Padding(0, 5, 8, 0)
    $ui.Folder.TabIndex = 0
    $ui.Folder.AccessibleName = 'Game folder'
    $folderRow.Controls.Add($ui.Folder, 0, 0)
    $browse = New-SetupButton '&Browse...' 1
    $detect = New-SetupButton 'D&etect' 2
    foreach ($button in @($browse, $detect)) {
        $button.AutoSize = $false
        $button.MinimumSize = New-Object Drawing.Size(0, 34)
        $button.Dock = 'Fill'
        $button.Margin = New-Object Windows.Forms.Padding(0, 0, 6, 6)
    }
    $folderRow.Controls.Add($browse, 1, 0)
    $folderRow.Controls.Add($detect, 2, 0)
    $ui.FolderRow = $folderRow
    $game.Controls.Add($folderRow, 0, 2)
    $buildCard = New-SetupCard
    $buildCard.Padding = New-Object Windows.Forms.Padding(8, 4, 8, 4)
    $buildCard.Margin = New-Object Windows.Forms.Padding(0, 2, 6, 0)
    $buildCard.ColumnCount = 2
    $buildCard.RowCount = 1
    [void]$buildCard.RowStyles.Add((New-Object Windows.Forms.RowStyle('Percent', 100)))
    [void]$buildCard.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Absolute', 56)))
    [void]$buildCard.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Percent', 100)))
    $game.Controls.Add($buildCard, 0, 3)
    $ui.BuildIcon = New-Object Windows.Forms.Panel
    $ui.BuildIcon.Dock = 'Fill'
    $ui.BuildIcon.Tag = 'warning'
    $ui.BuildIcon.AccessibleName = 'Awaiting build verification'
    $ui.BuildIcon.Add_Paint({
        param($sender, $event)
        $g = $event.Graphics; $g.SmoothingMode = 'AntiAlias'
        $size = [single]([Math]::Min($sender.Width, $sender.Height) - 6)
        $x = [single](($sender.Width - $size) / 2); $y = [single](($sender.Height - $size) / 2)
        $brush = New-Object Drawing.SolidBrush($amber)
        $pen = New-Object Drawing.Pen($coal, [single]($size / 11))
        try {
            $g.FillEllipse($brush, $x, $y, $size, $size)
            if ($sender.Tag -eq 'check') {
                $g.DrawLines($pen, [Drawing.PointF[]]@(
                    (New-Object Drawing.PointF(($x + $size * .24), ($y + $size * .50))),
                    (New-Object Drawing.PointF(($x + $size * .43), ($y + $size * .69))),
                    (New-Object Drawing.PointF(($x + $size * .77), ($y + $size * .32)))))
            } else {
                $g.DrawLine($pen, [single]($x + $size * .5), [single]($y + $size * .22),
                    [single]($x + $size * .5), [single]($y + $size * .57))
                $g.DrawLine($pen, [single]($x + $size * .5), [single]($y + $size * .70),
                    [single]($x + $size * .5), [single]($y + $size * .78))
            }
        } finally { $brush.Dispose(); $pen.Dispose() }
    })
    $buildCard.Controls.Add($ui.BuildIcon, 0, 0)
    $buildText = New-Object Windows.Forms.TableLayoutPanel
    $buildText.Dock = 'Fill'
    $buildText.ColumnCount = 1
    $buildText.RowCount = 2
    $buildText.Margin = New-Object Windows.Forms.Padding(0)
    [void]$buildText.RowStyles.Add((New-Object Windows.Forms.RowStyle('Percent', 50)))
    [void]$buildText.RowStyles.Add((New-Object Windows.Forms.RowStyle('Percent', 50)))
    $buildCard.Controls.Add($buildText, 1, 0)
    $ui.BuildStatus = Add-SetupLabel $buildText 'Awaiting game detection'
    $ui.BuildStatus.AccessibleName = 'Detected build'
    $ui.BuildStatus.Font = New-Object Drawing.Font('Segoe UI', 11, [Drawing.FontStyle]::Bold)
    $ui.BuildStatus.Margin = New-Object Windows.Forms.Padding(0)
    $ui.Status = Add-SetupLabel $buildText 'Choose a game folder or detect it from the registry.'
    $ui.Status.AutoSize = $false
    $ui.Status.Dock = 'Fill'
    $ui.Status.Margin = New-Object Windows.Forms.Padding(0)
    $ui.Status.AccessibleName = 'Detected game status'
    $ui.Status.ForeColor = $muted
    $primary = New-Object Windows.Forms.TableLayoutPanel
    $primary.ColumnCount = 1
    $primary.Dock = 'Fill'
    $primary.Padding = New-Object Windows.Forms.Padding(0, 36, 0, 0)
    [void]$primary.RowStyles.Add((New-Object Windows.Forms.RowStyle('Absolute', 72)))
    [void]$primary.RowStyles.Add((New-Object Windows.Forms.RowStyle('Percent', 100)))
    $primary.TabIndex = 1
    $installation.Controls.Add($primary, 1, 0)
    $ui.Install = New-SetupButton '&INSTALL  ›' 0
    $ui.Install.AccessibleName = 'Install action'
    $ui.Install.AutoSize = $false
    $ui.Install.Dock = 'Fill'
    $ui.Install.Margin = New-Object Windows.Forms.Padding(0)
    $ui.Install.Font = New-Object Drawing.Font($displayFace, 27, [Drawing.FontStyle]::Bold)
    $ui.Install.BackColor = $amber
    $ui.Install.ForeColor = $coal
    $ui.Install.FlatAppearance.BorderColor = $amber
    $ui.Install.FlatAppearance.MouseOverBackColor = [Drawing.Color]::FromArgb(255, 203, 136)
    $ui.Install.FlatAppearance.MouseDownBackColor = [Drawing.Color]::FromArgb(235, 160, 76)
    $primary.Controls.Add($ui.Install, 0, 0)
    $installNote = Add-SetupLabel $primary 'Install the BigFix community patch for Escape from Butcher Bay (2004).'
    $installNote.AutoSize = $false
    $installNote.Dock = 'Fill'
    $installNote.ForeColor = $muted
    $installNote.Margin = New-Object Windows.Forms.Padding(0, 8, 0, 0)

    $options = New-SetupCard
    $options.ColumnCount = 1
    $options.RowCount = 5
    $options.TabIndex = 1
    foreach ($height in @(32, 28, 38, 0, 68)) {
        $style = if ($height) { New-Object Windows.Forms.RowStyle('Absolute', $height) } else { New-Object Windows.Forms.RowStyle('Percent', 100) }
        [void]$options.RowStyles.Add($style)
    }
    $ui.Options = $options
    $root.Controls.Add($options, 0, 2)
    [void](Add-SetupHeading $options 'Configuration')
    $intro = Add-SetupLabel $options 'Recommended settings are selected. Adjust before installing.'
    $intro.ForeColor = $muted
    $intro.Margin = New-Object Windows.Forms.Padding(0, 3, 0, 0)
    $sections = New-Object Windows.Forms.TableLayoutPanel
    $sections.Dock = 'Fill'
    $sections.ColumnCount = 5
    $sections.Margin = New-Object Windows.Forms.Padding(0, 0, 0, 6)
    $sections.TabIndex = 0
    $options.Controls.Add($sections, 0, 2)
    $pages = New-Object Windows.Forms.Panel
    $pages.Dock = 'Fill'
    $pages.Margin = New-Object Windows.Forms.Padding(0)
    $pages.TabIndex = 1
    $options.Controls.Add($pages, 0, 3)
    $index = 0
    foreach ($item in @(@('Display', '&Display'), @('Graphics', '&Graphics'), @('Input', 'I&nput'),
        @('Audio', '&Audio'), @('Diagnostics', 'Diagnos&tics'))) {
        [void]$sections.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Percent', 20)))
        $button = New-SetupButton $item[1] $index
        $button.Dock = 'Fill'
        $button.AutoSize = $false
        $button.MinimumSize = New-Object Drawing.Size(0, 0)
        $button.Margin = New-Object Windows.Forms.Padding(0, 0, 4, 0)
        $button.Tag = $item[0]
        $button.AccessibleName = "$($item[0]) settings"
        $button.Add_Click({ param($sender, $event) Set-SetupSection ([string]$sender.Tag) })
        $sections.Controls.Add($button, $index, 0)
        $ui.Sections[$item[0]] = $button
        $page = New-Object Windows.Forms.TableLayoutPanel
        $page.Dock = 'Fill'
        $page.ColumnCount = 4
        $page.RowCount = 4
        $page.Padding = New-Object Windows.Forms.Padding(0, 4, 0, 0)
        $page.AccessibleName = "$($item[0]) options"
        foreach ($width in @(17, 36, 20, 27)) { [void]$page.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Percent', $width))) }
        for ($i = 0; $i -lt 4; $i++) { [void]$page.RowStyles.Add((New-Object Windows.Forms.RowStyle('Percent', 25))) }
        $page.Add_Paint({
            param($sender, $event)
            if ($sender.AccessibleName -eq 'Audio options') { return }
            $pen = New-Object Drawing.Pen($edge)
            try {
                $x = [int]($sender.Width * .53)
                $event.Graphics.DrawLine($pen, $x, 8, $x, ($sender.Height - 8))
            } finally { $pen.Dispose() }
        })
        $pages.Controls.Add($page)
        $ui.Pages[$item[0]] = $page
        $index++
    }
    $ui.WindowMode = New-SetupChoice @('borderless', 'fullscreen', 'windowed') 180
    $ui.WindowMode.AccessibleName = 'Window mode'
    Add-SetupField $ui.Pages.Display '&Window mode:' $ui.WindowMode 0 0
    $ui.FOV = New-SetupNumber 120
    $ui.FOV.AccessibleName = 'FOV'
    Add-SetupField $ui.Pages.Display 'Field of view (&FOV):' $ui.FOV 0 1 '0 = game default'
    $ui.FrameCap = New-SetupNumber ([decimal]::MaxValue)
    $ui.FrameCap.AccessibleName = 'FrameCap'
    Add-SetupField $ui.Pages.Display 'Frame &cap (FPS):' $ui.FrameCap 0 2 '0 = no limit'
    $ui.Desktop = New-SetupChoice @('Automatic (recommended)') 180
    # Native editable combos paint a light edit field on some Windows themes.
    # Use the same owner-drawn list as Window mode, with an explicit custom entry.
    [void]$ui.Desktop.Items.Add('Custom...')
    $ui.Desktop.Add_SelectionChangeCommitted({
        if ($ui.Desktop.SelectedItem -eq 'Custom...') {
            Add-Type -AssemblyName Microsoft.VisualBasic
            $mode = [Microsoft.VisualBasic.Interaction]::InputBox('Enter WIDTHxHEIGHT@HZ (for example 1920x1080@60).', 'Desktop mode', '')
            if ($mode) {
                if (-not $ui.Desktop.Items.Contains($mode)) { [void]$ui.Desktop.Items.Add($mode) }
                $ui.Desktop.SelectedItem = $mode
            } else { $ui.Desktop.SelectedIndex = 0 }
        }
    })
    $ui.Desktop.AccessibleName = 'Desktop mode'
    $ui.Desktop.AccessibleDescription = 'Automatic, detected mode, or choose Custom to enter WIDTHxHEIGHT@HZ.'
    if ($InitialState.DesktopMode) {
        [void]$ui.Desktop.Items.Add([string]$InitialState.DesktopMode)
        $ui.Desktop.SelectedItem = [string]$InitialState.DesktopMode
    }
    Add-SetupField $ui.Pages.Display 'Desktop &mode:' $ui.Desktop 0 3
    [void](Add-SetupCheck $ui.Pages.Display 'HudAspectFix' '&HUD / subtitle fix' 2 0 'Restore HUD scaling and subtitle display.')
    [void](Add-SetupCheck $ui.Pages.Display 'VideoAspectFix' '&Video aspect fix' 2 1 'Keep videos at the correct aspect ratio.')
    [void](Add-SetupCheck $ui.Pages.Display 'DisplayModeFilter' 'Display mode filte&r' 2 2 'Show suitable resolutions for this display.')
    $ui.FOV.Add_ValueChanged({
        if ($ui.FOV.Value -gt 0 -and $ui.FOV.Value -lt 60) {
            $ui.FOV.Value = if ($ui.FOV.Tag -eq 60) { 0 } else { 60 }
        }
        $ui.FOV.Tag = $ui.FOV.Value
    })
    $ui.FOV.AccessibleDescription = '0 = game default; otherwise 60 to 120. May affect hands and weapons.'
    $ui.FrameCap.AccessibleDescription = '0 = no limit; otherwise at least 1 FPS.'
    $ui.Shader = New-SetupChoice @('2.0 (recommended)', '2.0++ (experimental)') 205
    $ui.Shader.AccessibleName = 'Shader mode'
    Add-SetupField $ui.Pages.Graphics '&Shader mode:' $ui.Shader 0 0
    $ui.AntiAliasing = New-SetupChoice @('0', '2', '4', '8') 150
    $ui.AntiAliasing.AccessibleName = 'Anti-aliasing samples (0 keeps game selection)'
    Add-SetupField $ui.Pages.Graphics 'AA sa&mples:' $ui.AntiAliasing 0 1
    [void](Add-SetupCheck $ui.Pages.Graphics 'TextureSizeFix' 'High resolution fi&x' 0 2 'Allow textures at their full resolution.')
    [void](Add-SetupCheck $ui.Pages.Graphics 'Enable20PlusPlusAnyGpu' '2.0++ on any G&PU' 0 3 'Allow the experimental shader on any GPU.')
    [void](Add-SetupCheck $ui.Pages.Graphics 'FixMiscBugs' 'Driver &compatibility' 2 0 'Apply fixes for modern graphics drivers.')
    [void](Add-SetupCheck $ui.Pages.Graphics 'EyeshineFix' '&Eyeshine fix' 2 1 'Correct Eyeshine rendering on modern GPUs.')
    [void](Add-SetupCheck $ui.Pages.Graphics 'EyeshineFlickerWorkaround' 'Legacy shadow &workaround' 2 2 'Reduce flickering in the legacy shadow path.')
    [void](Add-SetupNote $ui.Pages.Graphics 'AA 0 = game selection. 2.0++ needs visual checks.' 3 2 2)
    [void](Add-SetupCheck $ui.Pages.Input 'RawMouse' '&Raw mouse input' 0 0 'Use raw mouse input for camera control.')
    $audioLabel = Add-SetupLabel $ui.Pages.Audio 'Audio &backend:'
    $ui.Pages.Audio.SetCellPosition($audioLabel, (New-Object Windows.Forms.TableLayoutPanelCellPosition(0, 0)))
    $ui.SystemAudio = New-Object Windows.Forms.RadioButton
    $ui.SystemAudio.Text = '&Windows audio (default)'
    $ui.SystemAudio.Checked = $true
    $ui.SystemAudio.AutoSize = $true
    $ui.SystemAudio.AccessibleName = 'Windows audio (default)'
    $ui.Pages.Audio.Controls.Add($ui.SystemAudio, 1, 0)
    $ui.Pages.Audio.SetColumnSpan($ui.SystemAudio, 3)
    $ui.Dsoal = New-Object Windows.Forms.RadioButton
    $ui.Dsoal.Text = 'DS&OAL + OpenAL Soft (restores EAX reverb, experimental)'
    $ui.Dsoal.AutoSize = $true
    $ui.Dsoal.AccessibleName = 'DSOAL + OpenAL Soft (restores EAX reverb, experimental)'
    $ui.Dsoal.AccessibleDescription = 'Optional audio backend. Switching requires uninstalling, then reinstalling.'
    $ui.Dsoal.Enabled = $false
    $ui.Pages.Audio.Controls.Add($ui.Dsoal, 1, 1)
    $ui.Pages.Audio.SetColumnSpan($ui.Dsoal, 3)
    # Preserve native radio keyboard/accessibility behavior while keeping disabled
    # text legible on the dark background (Windows otherwise draws a black shadow).
    foreach ($radio in @($ui.SystemAudio, $ui.Dsoal)) {
        $radio.AutoSize = $false
        $radio.Dock = 'Fill'
        $radio.Add_Paint({
            param($sender, $event)
            $size = $sender.Font.Height
            $x = 3; $y = [int](($sender.Height - $size) / 2)
            $bounds = New-Object Drawing.Rectangle(($size + 9), 0,
                ([Math]::Max(1, $sender.Width - $size - 9)), $sender.Height)
            $brush = New-Object Drawing.SolidBrush($sender.BackColor)
            $color = if ($sender.Enabled) { $bone } else { $muted }
            $pen = New-Object Drawing.Pen($color)
            $dot = New-Object Drawing.SolidBrush($amber)
            try {
                $event.Graphics.FillRectangle($brush, $sender.ClientRectangle)
                $event.Graphics.DrawEllipse($pen, $x, $y, $size, $size)
                if ($sender.Checked) { $event.Graphics.FillEllipse($dot, ($x + 3), ($y + 3), ($size - 6), ($size - 6)) }
            } finally { $brush.Dispose(); $pen.Dispose(); $dot.Dispose() }
            [Windows.Forms.TextRenderer]::DrawText($event.Graphics, $sender.Text, $sender.Font,
                $bounds, $color, [Windows.Forms.TextFormatFlags]'Left,VerticalCenter,SingleLine')
            if ($sender.Focused) {
                [Windows.Forms.ControlPaint]::DrawFocusRectangle($event.Graphics, $sender.ClientRectangle, $bone, $sender.BackColor)
            }
        })
    }
    $ui.AudioTooltip = New-Object Windows.Forms.ToolTip
    $ui.AudioAvailability = Add-SetupNote $ui.Pages.Audio 'Detect the game to check DSOAL package availability.' 2
    [void](Add-SetupCheck $ui.Pages.Audio 'BackgroundAudio' '&Background audio' 0 3 'Keep audio playing while the game is unfocused.')
    $ui.AudioInstalled = Add-SetupNote $ui.Pages.Audio 'Switching backend requires uninstalling, then reinstalling.' 3 2 2
    [void](Add-SetupCheck $ui.Pages.Diagnostics 'SkipLogoVideos' '&Skip startup logos' 0 0 'Go straight to the game without logo videos.')
    [void](Add-SetupCheck $ui.Pages.Diagnostics 'CapturePng' 'PNG &screenshots' 2 0 'Save captured screenshots in PNG format.')
    [void](Add-SetupCheck $ui.Pages.Diagnostics 'TraceEax' 'Trace &EAX' 0 1 'Log EAX reverb calls for troubleshooting; may produce a large log.')
    [void](Add-SetupNote $ui.Pages.Diagnostics 'Setup activity is logged automatically.' 2 0 2)
    $safety = New-Object Windows.Forms.TableLayoutPanel
    $safety.Dock = 'Fill'
    $safety.ColumnCount = 4
    $safety.Margin = New-Object Windows.Forms.Padding(0, 6, 0, 0)
    $safety.Padding = New-Object Windows.Forms.Padding(0, 6, 0, 0)
    $safety.TabIndex = 2
    foreach ($width in @(25, 28, 25, 22)) {
        [void]$safety.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Percent', $width)))
    }
    $safety.Add_Paint({
        param($sender, $event)
        $pen = New-Object Drawing.Pen($edge)
        try { $event.Graphics.DrawLine($pen, 0, 0, $sender.Width, 0) } finally { $pen.Dispose() }
    })
    $options.Controls.Add($safety, 0, 4)
    $ui.Preview = Add-SetupCheck $safety '' '&Preview only (do not modify files)' 0 0 'Show planned changes without installing.'
    $ui.Preview.Checked = [bool]$InitialState.WhatIf
    $ui.Repair = Add-SetupCheck $safety '' 'Allow registr&y path repair' 2 0 'Optional. Setup asks for consent before changes.'
    $ui.Repair.Checked = -not $InitialState.NoRegistryRepair
    Set-SetupSection 'Display'

    $statusStrip = New-SetupCard
    $statusStrip.Padding = New-Object Windows.Forms.Padding(12, 8, 12, 8)
    $statusStrip.Margin = New-Object Windows.Forms.Padding(0, 0, 0, 8)
    $statusStrip.ColumnCount = 2
    $statusStrip.RowCount = 2
    [void]$statusStrip.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Percent', 100)))
    [void]$statusStrip.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Absolute', 350)))
    [void]$statusStrip.RowStyles.Add((New-Object Windows.Forms.RowStyle('Percent', 100)))
    [void]$statusStrip.RowStyles.Add((New-Object Windows.Forms.RowStyle('Absolute', 6)))
    $statusStrip.TabIndex = 2
    $root.Controls.Add($statusStrip, 0, 3)
    $activity = New-Object Windows.Forms.TableLayoutPanel
    $activity.ColumnCount = 1
    $activity.RowCount = 2
    $activity.Dock = 'Fill'
    $activity.Margin = New-Object Windows.Forms.Padding(0)
    [void]$activity.RowStyles.Add((New-Object Windows.Forms.RowStyle('Absolute', 25)))
    [void]$activity.RowStyles.Add((New-Object Windows.Forms.RowStyle('Percent', 100)))
    $statusStrip.Controls.Add($activity, 0, 0)
    $ui.ActivityTitle = Add-SetupLabel $activity 'Ready'
    $ui.ActivityTitle.Font = New-Object Drawing.Font('Segoe UI', 12, [Drawing.FontStyle]::Bold)
    $ui.ActivityTitle.Margin = New-Object Windows.Forms.Padding(0)
    $ui.Activity = Add-SetupLabel $activity 'No changes made.'
    $ui.Activity.Margin = New-Object Windows.Forms.Padding(0)
    $ui.Activity.AutoSize = $false
    $ui.Activity.Dock = 'Fill'
    $ui.Activity.ForeColor = $muted
    $ui.Activity.AccessibleName = 'Setup activity status'
    $actions = New-Object Windows.Forms.FlowLayoutPanel
    $actions.Dock = 'Fill'
    $actions.WrapContents = $false
    $actions.Margin = New-Object Windows.Forms.Padding(0)
    $statusStrip.Controls.Add($actions, 1, 0)
    $ui.Launch = New-SetupButton 'La&unch game' 0
    $ui.Uninstall = New-SetupButton '&Uninstall' 1
    $ui.Close = New-SetupButton 'E&xit' 2
    foreach ($button in @($ui.Launch, $ui.Uninstall, $ui.Close)) {
        $button.MinimumSize = New-Object Drawing.Size(96, 32)
        $button.Margin = New-Object Windows.Forms.Padding(0, 0, 6, 0)
        $actions.Controls.Add($button)
    }
    # The backend reports lines, not percentages. Animate only an indeterminate
    # segment while it runs; never imply an invented completion percentage.
    $ui.Progress = New-Object Windows.Forms.Panel
    $ui.Progress.Dock = 'Fill'
    $ui.Progress.Margin = New-Object Windows.Forms.Padding(0, 2, 0, 0)
    $ui.Progress.Tag = 0
    $ui.Progress.AccessibleRole = 'ProgressBar'
    $ui.Progress.AccessibleName = 'Backend operation progress (indeterminate while running)'
    $ui.Progress.Add_Paint({
        param($sender, $event)
        $track = New-Object Drawing.SolidBrush($edge)
        $light = New-Object Drawing.SolidBrush($amber)
        try {
            $event.Graphics.FillRectangle($track, $sender.ClientRectangle)
            if ($ui.Process) {
                $width = [Math]::Max(1, [int]($sender.Width / 5))
                $x = [int](($sender.Width + $width) * [int]$sender.Tag / 1000) - $width
                $event.Graphics.FillRectangle($light, $x, 0, $width, $sender.Height)
            } else { $event.Graphics.FillRectangle($light, $sender.ClientRectangle) }
        } finally { $track.Dispose(); $light.Dispose() }
    })
    $statusStrip.Controls.Add($ui.Progress, 0, 1)
    $statusStrip.SetColumnSpan($ui.Progress, 2)
    $tools = New-Object Windows.Forms.TableLayoutPanel
    $tools.ColumnCount = 2
    $tools.Dock = 'Fill'
    $tools.Margin = New-Object Windows.Forms.Padding(0)
    $tools.TabIndex = 3
    [void]$tools.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Percent', 100)))
    [void]$tools.ColumnStyles.Add((New-Object Windows.Forms.ColumnStyle('Absolute', 120)))
    $root.Controls.Add($tools, 0, 4)
    $ui.LogToggle = New-SetupButton '▸  Show activity &log' 0
    $ui.LogToggle.AccessibleName = 'Toggle activity log'
    $ui.LogToggle.AccessibleDescription = 'Collapsed'
    $ui.LogToggle.FlatAppearance.BorderSize = 0
    $ui.LogToggle.BackColor = $coal
    $ui.LogToggle.TextAlign = 'MiddleLeft'
    $ui.LogToggle.AutoSize = $false
    $ui.LogToggle.Width = 240
    $ui.LogToggle.Add_Click({ Set-SetupLogExpanded (-not $ui.LogExpanded) })
    $tools.Controls.Add($ui.LogToggle, 0, 0)
    $ui.OpenLog = New-SetupButton '&Open log' 1
    $ui.OpenLog.Margin = New-Object Windows.Forms.Padding(0)
    $tools.Controls.Add($ui.OpenLog, 1, 0)
    $ui.Log = New-Object Windows.Forms.TextBox
    $ui.Log.Multiline = $true
    $ui.Log.Dock = 'Fill'
    $ui.Log.ReadOnly = $true
    $ui.Log.BackColor = $coal
    $ui.Log.ForeColor = $bone
    $ui.Log.BorderStyle = 'FixedSingle'
    $ui.Log.Font = New-Object Drawing.Font('Consolas', 9)
    $ui.Log.ScrollBars = 'Both'
    $ui.Log.WordWrap = $false
    $ui.Log.AccessibleName = 'Setup log'
    $ui.Log.TabIndex = 4
    $ui.Log.Visible = $false
    $root.Controls.Add($ui.Log, 0, 5)
    $footer = Add-SetupLabel $root 'Community made. No game files included. Original files are backed up by the installer.'
    $footer.ForeColor = $muted
    $footer.Font = New-Object Drawing.Font('Segoe UI', 9)
    $root.SetCellPosition($footer, (New-Object Windows.Forms.TableLayoutPanelCellPosition(0, 6)))
    $form.AcceptButton = $ui.Install
    $form.CancelButton = $ui.Close

    $ui.Timer = New-Object Windows.Forms.Timer
    $ui.Timer.Interval = 60
    $ui.Timer.Add_Tick({
        try {
            $ui.Progress.Tag = ([int]$ui.Progress.Tag + 16) % 1000
            $ui.Progress.Invalidate()
            foreach ($stream in @(@('ReadOut', 'Output', 'StandardOutput'), @('ReadErr', 'Errors', 'StandardError'))) {
                # Drain both pipes without worker-thread PowerShell event handlers or DoEvents.
                $task = $ui[$stream[0]]
                $count = 0
                while ($null -ne $task -and $task.IsCompleted -and $count -lt 100) {
                    $line = $task.GetAwaiter().GetResult()
                    if ($null -eq $line) { $ui[$stream[0]] = $null; break }
                    [void]$ui[$stream[1]].AppendLine($line)
                    if ($ui.Action -ne 'Inspect') { Write-SetupLog $line }
                    $task = $ui.Process.($stream[2]).ReadLineAsync()
                    $ui[$stream[0]] = $task
                    $count++
                }
            }
            if (-not $ui.Process.HasExited -or $ui.ReadOut -or $ui.ReadErr) { return }
            $ui.Timer.Stop()
            $code = $ui.Process.ExitCode
            $ui.Process.Dispose()
            $ui.Process = $null
            if ($ui.Action -eq 'Inspect') {
                if ($code -eq 0) { Set-SetupInspection $ui.Output.ToString() }
                else {
                    Set-SetupDetectionFailure ($ui.Errors.ToString() + $ui.Output.ToString())
                }
                Update-SetupActions
            } else {
                Write-SetupLog "Backend finished with exit code $code."
                $ui.LastResult = if ($code -ne 0) { "$($ui.Action) failed. Open the activity log for details." }
                    elseif ($ui.Preview.Checked) { 'Preview complete. No changes made.' }
                    else { "$($ui.Action) complete." }
                if ($code -ne 0) {
                    Set-SetupLogExpanded $true
                    Show-SetupError ("$($ui.Action) failed (exit $code).`r`n" + $ui.Errors.ToString() + $ui.Output.ToString())
                }
                Request-SetupInspection
            }
        } catch {
            $ui.Timer.Stop()
            # Keep the process reference if it is still running: never enable a second install.
            if ($ui.Process -and $ui.Process.HasExited) { $ui.Process.Dispose(); $ui.Process = $null }
            Show-SetupError $_.Exception.Message
            Update-SetupActions
        }
    })
    $browse.Add_Click({
        $dialog = New-Object Windows.Forms.FolderBrowserDialog
        $dialog.Description = 'Choose the game folder containing System\Win64_AMD64\SbzEngine.exe'
        $dialog.ShowNewFolderButton = $false
        try {
            if ($dialog.ShowDialog($form) -eq 'OK') { $ui.Folder.Text = $dialog.SelectedPath; Request-SetupInspection }
        } catch { Show-SetupError $_.Exception.Message } finally { $dialog.Dispose() }
    })
    $detect.Add_Click({ try { Request-SetupInspection } catch { Show-SetupError $_.Exception.Message } })
    $ui.Folder.Add_TextChanged({
        $ui.Info = $null
        $ui.LastResult = ''
        $ui.BuildStatus.Text = 'Folder changed'
        $ui.BuildIcon.Tag = 'warning'
        $ui.BuildIcon.Invalidate()
        $ui.Status.Text = 'Choose Detect to check this installation.'
        Update-SetupActions
    })
    $ui.Install.Add_Click({ Invoke-SetupInstall $false })
    $ui.Uninstall.Add_Click({ Invoke-SetupInstall $true })
    $ui.Launch.Add_Click({
        try {
            $directory = Join-Path $ui.Info.GameDir 'System\Win64_AMD64'
            Start-Process -FilePath (Join-Path $directory 'SbzEngine.exe') -WorkingDirectory $directory
        } catch { Show-SetupError $_.Exception.Message }
    })
    $ui.OpenLog.Add_Click({ try { Start-Process -FilePath 'notepad.exe' -ArgumentList ('"' + $ui.LogPath + '"') } catch { Show-SetupError $_.Exception.Message } })
    $ui.Close.Add_Click({ $form.Close() })
    $form.Add_FormClosing({
        param($sender, $event)
        if ($ui.Process) { $event.Cancel = $true; Show-SetupError 'Wait for the current operation to finish before closing setup.' }
    })
    Set-SetupSettings (Get-BigFixSetupDefaults)
    Update-SetupActions
    Write-SetupLog 'BigFix setup ready. Detecting the game is read-only.'
    Write-SetupLog ('Setup log: ' + $ui.LogPath)
    if (-not $CapturePath) {
        $workArea = [Windows.Forms.Screen]::FromControl($form).WorkingArea
        $form.Size = New-Object Drawing.Size(
            ([Math]::Min($form.Width, $workArea.Width)), ([Math]::Min($form.Height, $workArea.Height)))
    }
    try {
        if ($CapturePath) {
            # No visible window or game writes. Drain both pipes before rendering.
            $argsMap = Get-BigFixSetupArguments $InitialState
            $argsMap.Remove('IniOverrides'); $argsMap.Remove('ShaderMode'); $argsMap.Remove('Uninstall')
            $argsMap.Inspect = $true
            $process = New-BigFixBackendProcess $argsMap
            try {
                $outTask = $process.StandardOutput.ReadToEndAsync()
                $errTask = $process.StandardError.ReadToEndAsync()
                $process.WaitForExit()
                if ($process.ExitCode -eq 0) { Set-SetupInspection $outTask.GetAwaiter().GetResult() }
                else { Set-SetupDetectionFailure $errTask.GetAwaiter().GetResult() }
            } finally { $process.Dispose() }
            Update-SetupActions
            # Native combo/edit controls need a visible control hierarchy to print.
            # Realize it fully transparent and off-screen, then draw synchronously.
            $form.ShowInTaskbar = $false
            $form.StartPosition = 'Manual'
            $form.Location = New-Object Drawing.Point(-32000, -32000)
            $form.Opacity = 0
            $form.Show()
            if ($ui.Install.Enabled) { $form.ActiveControl = $ui.Install }
            # Capture DPI is a repeatable layout simulation, independent of monitor
            # settings. Scale controls AND fonts; resizing the bitmap alone hides clipping.
            $factor = $CaptureDpi / [double]$form.DeviceDpi
            $ui.LayoutScale = $CaptureDpi / 96.0
            if ([Math]::Abs($factor - 1) -gt 0.01) {
                $fonts = New-Object Collections.Generic.List[object]
                $pending = New-Object Collections.Queue
                $pending.Enqueue($form)
                while ($pending.Count) {
                    $control = $pending.Dequeue()
                    $fonts.Add(@{ Control = $control; Font = $control.Font })
                    foreach ($child in $control.Controls) { $pending.Enqueue($child) }
                }
                $form.AutoScaleMode = 'None'
                $form.SuspendLayout()
                $form.Scale((New-Object Drawing.SizeF($factor, $factor)))
                foreach ($entry in $fonts) {
                    $font = $entry.Font
                    $entry.Control.Font = New-Object Drawing.Font($font.FontFamily, ([single]($font.Size * $factor)), $font.Style)
                }
                $form.ResumeLayout($true)
            }
            Set-SetupSection $CaptureSection
            $form.PerformLayout()
            $ui.Desktop.SelectionLength = 0
            $form.Refresh()
            $bitmap = New-Object Drawing.Bitmap($form.Width, $form.Height)
            try {
                $form.DrawToBitmap($bitmap, (New-Object Drawing.Rectangle(0, 0, $form.Width, $form.Height)))
                $bitmap.Save([IO.Path]::GetFullPath($CapturePath), [Drawing.Imaging.ImageFormat]::Png)
            } finally { $bitmap.Dispose() }
        } else {
            $form.Add_Shown({
                $ui.LayoutScale = $form.DeviceDpi / 96.0
                try { Request-SetupInspection } catch { Show-SetupError $_.Exception.Message }
            })
            if ($form.GetType().GetEvent('DpiChanged')) {
                $form.Add_DpiChanged({
                    $ui.LayoutScale = $form.DeviceDpi / 96.0
                    Set-SetupLogExpanded $ui.LogExpanded
                })
            }
            [void]$form.ShowDialog()
        }
    } finally { $ui.Timer.Dispose(); $ui.AudioTooltip.Dispose(); $form.Dispose() }
}

if ($MyInvocation.InvocationName -eq '.') { return }

$state = @{
    GameDir = $GameDir; SourceDir = $SourceDir; DataFile = $DataFile; RegistryFile = $RegistryFile
    ShortcutDir = $ShortcutDir; DesktopMode = $DesktopMode; NoRegistryRepair = [bool]$NoRegistryRepair
    WhatIf = [bool]$WhatIf; Uninstall = [bool]$Uninstall
    AudioBackend = $AudioBackend; BackupAudioConflicts = [bool]$BackupAudioConflicts
}
if ($NoGui) {
    @{ Script = $script:SetupBackend; Arguments = (Get-BigFixSetupArguments $state) } | ConvertTo-Json -Depth 6
    return
}
try {
    Show-BigFixSetup $state $ScreenshotPath $ScreenshotDpi $ScreenshotSection
} catch {
    # Assembly/platform errors also remain legible when started from the launcher.
    if (-not $ScreenshotPath -and ('Windows.Forms.MessageBox' -as [type])) {
        [void][Windows.Forms.MessageBox]::Show($_.Exception.Message, 'BigFix setup could not start', 'OK', 'Error')
    }
    throw
}

@echo off
setlocal
start "" /wait powershell.exe -NoProfile -STA -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0BigFixSetup.ps1" %*
if errorlevel 1 (
    echo BigFix setup could not start. Run install.ps1 in PowerShell for diagnostics.
    pause
)

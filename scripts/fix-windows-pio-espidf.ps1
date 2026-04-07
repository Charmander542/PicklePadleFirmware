# Repair PlatformIO + ESP-IDF hybrid builds on Windows when the default Python is 3.14+.
# ESP-IDF 4.4.x pip deps (pydantic-core) need wheels that are not available for 3.14; pip then
# tries to compile Rust and fails, leaving idf_component_manager missing.
#
# Usage (from repo root, PowerShell):
#   .\scripts\fix-windows-pio-espidf.ps1
# Optional: -SkipBuild to only fix the venv / PlatformIO install.

param(
    [switch] $SkipBuild
)

$ErrorActionPreference = "Stop"

function Find-PyLauncherVersion {
    $list = @(py -0p 2>$null)
    if ($list.Count -eq 0) { return $null }
    $found = @()
    foreach ($line in $list) {
        if ($line -match '^\s*-V:3\.(1[123])\s+(.+\.exe)\s*$') {
            $found += @{ Major = [int]$matches[1]; Exe = $matches[2].Trim() }
        }
    }
    if ($found.Count -eq 0) { return $null }
    return ($found | Sort-Object -Property Major -Descending | Select-Object -First 1)
}

$py = Find-PyLauncherVersion
if ($null -eq $py) {
    Write-Host @"
No Python 3.11, 3.12, or 3.13 found via the 'py' launcher.

Install one of them, for example:
  winget install --id Python.Python.3.12 -e --accept-package-agreements --accept-source-agreements

Then re-run this script. Use builds with:
  py -3.12 -m platformio run
"@
    exit 1
}

Write-Host "Using Python 3.$($py.Major) at $($py.Exe)"

if (-not $SkipBuild) {
    $espidfVenv = Join-Path $env:USERPROFILE ".platformio\penv\.espidf-4.4.7"
    if (Test-Path $espidfVenv) {
        Write-Host "Removing ESP-IDF venv (recreates on next build): $espidfVenv"
        Remove-Item -Recurse -Force $espidfVenv
    }
}

Write-Host "Installing / upgrading PlatformIO for this Python..."
& py "-3.$($py.Major)" -m pip install -U pip platformio

if ($SkipBuild) {
    Write-Host "SkipBuild: done. Build with: py -3.$($py.Major) -m platformio run"
    exit 0
}

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
Set-Location $root
Write-Host "Building from $root ..."
& py "-3.$($py.Major)" -m platformio run

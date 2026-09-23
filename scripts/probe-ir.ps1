[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
$probe = Join-Path $root 'build\umbracam-probe.exe'
if (-not (Test-Path -LiteralPath $probe)) {
    throw 'Build UmbraCam first: .\scripts\build-native.ps1'
}

# The native probe uses the exact MTA + Direct3D-surface conversion path as
# the publisher. PowerShell's WinRT projection cannot reliably expose the
# frame map on newer Realtek Windows Hello camera stacks.
& $probe
if ($LASTEXITCODE -ne 0) {
    throw "Native IR probe failed with exit code $LASTEXITCODE"
}

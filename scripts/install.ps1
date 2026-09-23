[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Administrator)) {
    $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`""
    $elevated = Start-Process -FilePath powershell.exe -Verb RunAs -ArgumentList $arguments -Wait -PassThru
    exit $elevated.ExitCode
}

$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root 'build\IRCameraBridgeVCam.dll'
$host = Join-Path $root 'build\umbracam-host.exe'
if (-not (Test-Path -LiteralPath $dll)) {
    throw "Build the project first: .\scripts\build-native.ps1"
}
if (-not (Test-Path -LiteralPath $host)) {
    throw "Build the project first: .\scripts\build-native.ps1"
}

& "$env:WINDIR\System32\regsvr32.exe" /s $dll
if ($LASTEXITCODE -ne 0) {
    throw "Virtual-camera registration failed with exit code $LASTEXITCODE"
}

& $host --install
if ($LASTEXITCODE -ne 0) {
    throw "Virtual-camera activation failed with exit code $LASTEXITCODE"
}

Write-Host 'UmbraCam is installed. Launch .\build\umbracam.exe to publish IR frames to the UmbraCam IR Camera device.'

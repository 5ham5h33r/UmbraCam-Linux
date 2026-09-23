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
    Start-Process -FilePath powershell.exe -Verb RunAs -ArgumentList $arguments -Wait
    exit $LASTEXITCODE
}

$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root 'build\IRCameraBridgeVCam.dll'
if (-not (Test-Path -LiteralPath $dll)) {
    throw "Build the project first: .\scripts\build-native.ps1"
}

& "$env:WINDIR\System32\regsvr32.exe" /s $dll
if ($LASTEXITCODE -ne 0) {
    throw "Virtual-camera registration failed with exit code $LASTEXITCODE"
}

Write-Host 'IR Camera Bridge is installed. Launch .\build\ir_camera-bridge.exe to request camera access and publish the camera.'

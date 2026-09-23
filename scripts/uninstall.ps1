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
    throw "The built DLL was not found: $dll"
}
if (-not (Test-Path -LiteralPath $host)) {
    throw "The built installer helper was not found: $host"
}

& $host --uninstall
if ($LASTEXITCODE -ne 0) {
    throw "Virtual-camera removal failed with exit code $LASTEXITCODE"
}

& "$env:WINDIR\System32\regsvr32.exe" /u /s $dll
if ($LASTEXITCODE -ne 0) {
    throw "Virtual-camera removal failed with exit code $LASTEXITCODE"
}

Write-Host 'UmbraCam was removed.'

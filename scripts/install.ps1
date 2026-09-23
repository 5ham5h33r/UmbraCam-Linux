[CmdletBinding()]
param()

& (Join-Path $PSScriptRoot 'setup-camera.ps1') -Action install
exit $LASTEXITCODE

[CmdletBinding()]
param()

& (Join-Path $PSScriptRoot 'setup-camera.ps1') -Action uninstall
exit $LASTEXITCODE

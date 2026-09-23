[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('install', 'uninstall')]
    [string]$Action,
    [switch]$Elevated,
    [string]$LogPath
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$dll = Join-Path $root 'build\IRCameraBridgeVCam.dll'
$cameraHelper = Join-Path $root 'build\umbracam-host.exe'
$probe = Join-Path $root 'build\umbracam-probe.exe'
$identity = [Security.Principal.WindowsIdentity]::GetCurrent()
$principal = [Security.Principal.WindowsPrincipal]::new($identity)
$isAdministrator = $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)

if (-not $Elevated) {
    foreach ($requiredFile in @($dll, $cameraHelper, $probe)) {
        if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
            throw "Missing $requiredFile. Run .\scripts\build-native.ps1 first."
        }
    }
    $LogPath = Join-Path $root ('build\setup-' + [guid]::NewGuid().ToString('N') + '.log')
    if (-not $isAdministrator) {
        $arguments = "-NoProfile -ExecutionPolicy Bypass -File `"$PSCommandPath`" -Action $Action -Elevated -LogPath `"$LogPath`""
        Write-Host "Requesting Windows administrator approval for UmbraCam $Action..."
        try {
            $child = Start-Process -FilePath "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -Verb RunAs -WindowStyle Hidden -ArgumentList $arguments -PassThru
            # Cache the process handle before waiting so ExitCode remains available.
            $null = $child.Handle
            $child.WaitForExit()
            $setupExitCode = $child.ExitCode
        } catch {
            throw "Windows could not launch UmbraCam setup: $($_.Exception.Message)"
        }
        if (Test-Path -LiteralPath $LogPath) {
            Get-Content -LiteralPath $LogPath | Write-Host
        }
        if ($null -eq $setupExitCode -or $setupExitCode -ne 0) {
            throw "UmbraCam $Action failed (exit code: $setupExitCode). Log: $LogPath"
        }
        exit 0
    }
}

if (-not $isAdministrator) {
    throw 'UmbraCam setup requires a Windows administrator token.'
}
if (-not $LogPath) {
    throw 'Setup log path is missing.'
}

$setupExitCode = 1
$transcriptStarted = $false
try {
    Start-Transcript -LiteralPath $LogPath -Force | Out-Null
    $transcriptStarted = $true
    if ($Action -eq 'install') {
        $registration = Start-Process -FilePath "$env:WINDIR\System32\regsvr32.exe" -ArgumentList "/s `"$dll`"" -Wait -PassThru
        if ($registration.ExitCode -ne 0) {
            throw "Media-source registration failed: $($registration.ExitCode)"
        }
        & $cameraHelper --install
        if ($LASTEXITCODE -ne 0) {
            throw "Virtual-camera activation failed: $LASTEXITCODE"
        }
        & $probe --check-virtual-camera
        if ($LASTEXITCODE -ne 0) {
            throw 'Windows did not enumerate UmbraCam after installation.'
        }
        Write-Host 'UmbraCam installed and enumerated successfully.'
    } else {
        & $cameraHelper --uninstall
        if ($LASTEXITCODE -ne 0) {
            throw "Virtual-camera removal failed: $LASTEXITCODE"
        }
        $registration = Start-Process -FilePath "$env:WINDIR\System32\regsvr32.exe" -ArgumentList "/u /s `"$dll`"" -Wait -PassThru
        if ($registration.ExitCode -ne 0) {
            throw "Media-source unregistration failed: $($registration.ExitCode)"
        }
        Write-Host 'UmbraCam removed successfully.'
    }
    $setupExitCode = 0
} catch {
    Write-Host "ERROR: $($_.Exception.Message)"
} finally {
    if ($transcriptStarted) { Stop-Transcript | Out-Null }
}
exit $setupExitCode

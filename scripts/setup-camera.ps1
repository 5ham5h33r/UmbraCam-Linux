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
        # FrameServer runs as LocalService, which cannot read private user
        # profile directories. Copy the runtime into a protected machine
        # location instead of granting access to the user's Documents tree.
        $runtimeRoot = Join-Path $env:ProgramFiles 'UmbraCam'
        $version = (Get-FileHash -LiteralPath $dll -Algorithm SHA256).Hash.ToLowerInvariant()
        $runtimeDirectory = Join-Path $runtimeRoot $version
        $null = New-Item -ItemType Directory -Path $runtimeDirectory -Force
        $runtimeAcl = Get-Acl -LiteralPath $runtimeDirectory
        $serviceSid = [Security.Principal.SecurityIdentifier]::new('S-1-5-19')
        $serviceRead = [Security.AccessControl.FileSystemAccessRule]::new(
            $serviceSid, 'ReadAndExecute', 'ContainerInherit,ObjectInherit', 'None', 'Allow')
        $runtimeAcl.SetAccessRule($serviceRead)
        Set-Acl -LiteralPath $runtimeDirectory -AclObject $runtimeAcl
        foreach ($runtimeFile in @($dll, $cameraHelper, $probe)) {
            $destination = Join-Path $runtimeDirectory (Split-Path -Leaf $runtimeFile)
            # Do not overwrite a DLL already loaded by FrameServer when the
            # installed bytes are identical. Changed DLLs get a new directory.
            if (-not (Test-Path -LiteralPath $destination) -or
                (Get-FileHash -LiteralPath $destination).Hash -ne (Get-FileHash -LiteralPath $runtimeFile).Hash) {
                Copy-Item -LiteralPath $runtimeFile -Destination $destination -Force
            }
        }
        $dll = Join-Path $runtimeDirectory 'IRCameraBridgeVCam.dll'
        Write-Host "Registering media source: $dll"
        $registration = Start-Process -FilePath "$env:WINDIR\System32\regsvr32.exe" -ArgumentList "/s `"$dll`"" -PassThru
        $null = $registration.Handle
        $registration.WaitForExit()
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
        $registeredSource = Get-ItemProperty -LiteralPath 'HKLM:\SOFTWARE\Classes\CLSID\{19E90705-FD06-4443-9F30-68F97F09FC9E}\InprocServer32' -ErrorAction SilentlyContinue
        if ($registeredSource -and (Test-Path -LiteralPath $registeredSource.'(default)')) {
            $dll = $registeredSource.'(default)'
        }
        $registration = Start-Process -FilePath "$env:WINDIR\System32\regsvr32.exe" -ArgumentList "/u /s `"$dll`"" -PassThru
        $null = $registration.Handle
        $registration.WaitForExit()
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

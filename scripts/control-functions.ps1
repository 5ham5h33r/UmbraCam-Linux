# Shared menu operations. Dot-sourcing this file never opens a camera or installs anything.
Set-StrictMode -Version Latest
$script:UmbraRoot = Split-Path -Parent $PSScriptRoot

function Get-UmbraPublishers {
    $session = (Get-Process -Id $PID).SessionId
    @(Get-Process -Name umbracam -ErrorAction SilentlyContinue |
        Where-Object { $_.SessionId -eq $session })
}

function Confirm-UmbraAction([string]$Message) {
    (Read-Host "$Message [y/N]") -match '^(?i:y|yes)$'
}

function Invoke-UmbraScript([string]$Name) {
    # Run in a child: existing setup scripts use exit, which must not close the menu.
    $allowed = @('install.ps1', 'uninstall.ps1', 'build-native.ps1')
    if ($Name -notin $allowed) { throw 'Unsupported setup script.' }
    $path = Join-Path $script:UmbraRoot "scripts\$Name"
    & "$env:WINDIR\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File $path
    if ($LASTEXITCODE -ne 0) { throw "$Name failed (exit $LASTEXITCODE)." }
}

function Test-UmbraBuild {
    foreach ($name in @('umbracam.exe', 'umbracam-host.exe', 'umbracam-probe.exe', 'IRCameraBridgeVCam.dll')) {
        if (-not (Test-Path -LiteralPath (Join-Path $script:UmbraRoot "build\$name") -PathType Leaf)) { return $false }
    }
    return $true
}

function Build-Umbra {
    if (@(Get-UmbraPublishers).Count) { throw 'Disable UmbraCam before rebuilding its executable.' }
    Write-Host 'Building locally. Requires Visual Studio 2022 C++ Build Tools and Windows SDK 10.0.26100.0.'
    Invoke-UmbraScript 'build-native.ps1'
    if (-not (Test-UmbraBuild)) { throw 'Build finished without all required runtime files.' }
}

function Require-UmbraBuild {
    if (Test-UmbraBuild) { return }
    if (-not (Confirm-UmbraAction 'Native binaries are missing. Build them now?')) {
        throw 'No changes made. Use Build after installing the prerequisites listed in README.md.'
    }
    Build-Umbra
}

function Get-UmbraRegistration {
    $probe = Join-Path $script:UmbraRoot 'build\umbracam-probe.exe'
    if (-not (Test-Path -LiteralPath $probe -PathType Leaf)) { return 'Unknown (build required)' }
    # Enumeration only: does not open either physical camera or a virtual stream.
    $output = @(& $probe --check-virtual-camera 2>&1)
    $code = $LASTEXITCODE
    if ($code -eq 0 -and ($output -match '^Virtual camera enumeration: PASS$')) { return 'Installed' }
    if ($code -eq 1 -and ($output -match '^Virtual camera enumeration: NOT FOUND$')) { return 'Not installed' }
    throw "Camera enumeration failed (exit $code): $($output -join ' ')"
}

function Show-UmbraStatus {
    Write-Host "Repository: $script:UmbraRoot"
    Write-Host "Virtual camera: $(Get-UmbraRegistration)"
    $publishers = @(Get-UmbraPublishers)
    if ($publishers.Count) {
        Write-Host "Publisher: running in this Windows session (PID $($publishers.Id -join ', '))."
        Write-Host 'Stream health is not checked here. Select UmbraCam IR Camera in your call app.'
    } else { Write-Host 'Publisher: stopped in this Windows session; UmbraCam is not capturing IR.' }
}

function Install-Umbra {
    Require-UmbraBuild
    Invoke-UmbraScript 'install.ps1'
    if ((Get-UmbraRegistration) -ne 'Installed') { throw 'Setup returned but the virtual camera was not found.' }
    Write-Host 'Virtual camera installed. Enable it when you want to capture IR.'
}

function Start-UmbraPublisher {
    $publisher = Join-Path $script:UmbraRoot 'build\umbracam.exe'
    $child = Start-Process -FilePath $publisher -WorkingDirectory $script:UmbraRoot -WindowStyle Hidden -PassThru
    try {
        $null = $child.Handle
        if ($child.WaitForExit(2000)) {
            # The current native publisher can return 0 on capture failure, so
            # early exit is a failure regardless of the numeric exit code.
            throw 'Publisher exited during startup. Check build\umbracam-diagnostic.log and Windows camera privacy settings.'
        }
    } finally { $child.Dispose() }
}

function Enable-Umbra {
    # Also catches a publisher launched from another checkout in this session.
    if (@(Get-UmbraPublishers).Count) {
        Write-Host 'UmbraCam publisher is already running. Disable it before switching modes.'
        return
    }
    Require-UmbraBuild
    $registration = Get-UmbraRegistration
    if ($registration -eq 'Not installed') {
        if (-not (Confirm-UmbraAction 'Install the UmbraCam virtual camera now? Windows will request administrator approval.')) { return }
        Install-Umbra
    } elseif ($registration -ne 'Installed') { throw "Cannot enable: $registration" }
    Start-UmbraPublisher
    Write-Host 'Publisher launched (capture may still be initializing). Select UmbraCam IR Camera in your call app.'
    Write-Host 'Use Disable after the call to release the IR sensor. Closing this menu does NOT stop capture.'
}

function Send-UmbraStop {
    # Same session-local event as native/include/ir_camera_protocol.h. This
    # works even if this checkout's build directory has been removed.
    $stop = $null
    try {
        $stop = [Threading.EventWaitHandle]::OpenExisting('Local\IRCameraBridgeVCam_Stop')
        $null = $stop.Set()
        return $true
    } catch [Threading.WaitHandleCannotBeOpenedException] {
        return $false
    } finally {
        if ($null -ne $stop) { $stop.Dispose() }
    }
}

function Wait-UmbraStopped([int]$TimeoutMilliseconds = 10000) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    do {
        if (@(Get-UmbraPublishers).Count -eq 0) { return $true }
        Start-Sleep -Milliseconds 100
    } while ($timer.ElapsedMilliseconds -lt $TimeoutMilliseconds)
    return $false
}

function Disable-Umbra {
    if (@(Get-UmbraPublishers).Count -eq 0) {
        Write-Host 'UmbraCam publisher is already stopped.'
        return
    }
    if (-not (Send-UmbraStop)) { throw 'Publisher found but its stop event is unavailable. No processes were forcibly killed.' }
    if (-not (Wait-UmbraStopped)) {
        throw 'Publisher did not exit within 10 seconds. It may still hold the IR camera. Close camera clients and retry; no processes were forcibly killed.'
    }
    Write-Host 'UmbraCam capture stopped and publisher exited. The virtual camera stays listed.'
    Write-Host 'If the camera light remains on, another app or Windows Hello may still be using a sensor.'
}

function Uninstall-Umbra {
    Write-Host 'This removes the system-wide UmbraCam virtual camera for all users, not your physical camera driver.'
    Write-Host 'Close camera clients first. Versioned runtime files remain in Program Files for safety.'
    if ((Read-Host 'Type UNINSTALL to confirm') -cne 'UNINSTALL') { Write-Host 'Uninstall cancelled.'; return }
    Disable-Umbra
    Require-UmbraBuild
    Invoke-UmbraScript 'uninstall.ps1'
    if ((Get-UmbraRegistration) -ne 'Not installed') { throw 'Uninstall returned but camera removal could not be verified.' }
    Write-Host 'UmbraCam virtual camera uninstalled. OEM camera drivers were not changed.'
}

function Show-UmbraColor {
    Write-Host 'Live color mode is NOT available yet; no camera or GPU worker was started.'
    Write-Host 'Colorization currently runs offline on recorded clips. It is not connected to the virtual camera.'
    Write-Host 'Experimental results still have inaccurate colors and are not live-call ready.'
    Write-Host "Instructions: $(Join-Path $script:UmbraRoot 'experiments\colorization\README.md')"
}

function Invoke-UmbraAction([string]$Action) {
    switch ($Action) {
        'Enable' { Enable-Umbra }
        'Disable' { Disable-Umbra }
        'Status' { Show-UmbraStatus }
        'Install' { Install-Umbra }
        'Build' { Build-Umbra }
        'Uninstall' { Uninstall-Umbra }
        'Color' { Show-UmbraColor }
        default { throw "Unknown UmbraCam action: $Action" }
    }
}

function Get-UmbraMenuAction([string]$Choice) {
    switch ($Choice.Trim()) {
        '1' { 'Enable' }
        '2' { 'Disable' }
        '3' { 'Status' }
        '4' { 'Install' }
        '5' { 'Color' }
        '6' { 'Build' }
        '7' { 'Uninstall' }
        '0' { 'Exit' }
        default { 'Invalid' }
    }
}

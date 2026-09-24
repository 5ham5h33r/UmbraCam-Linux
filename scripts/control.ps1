[CmdletBinding()]
param([ValidateSet('Menu', 'Enable', 'Disable', 'Status', 'Install', 'Build', 'Uninstall', 'Color')][string]$Action = 'Menu')

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'control-functions.ps1')
$mutex = $null
$ownsMutex = $false
$result = 0
try {
    # Serialize launch/stop/setup actions across checkouts in this Windows session.
    $mutex = [Threading.Mutex]::new($false, 'Local\UmbraCam_ControlMenu')
    try { $ownsMutex = $mutex.WaitOne(0) }
    catch [Threading.AbandonedMutexException] { $ownsMutex = $true }
    if (-not $ownsMutex) { throw 'Another UmbraCam menu is open. Use that window, or close it first.' }
    if ($Action -ne 'Menu') { Invoke-UmbraAction $Action }
    else {
        do {
            Write-Host ''
            Write-Host '================ UmbraCam ================'
            Write-Host '  1  Enable IR camera'
            Write-Host '  2  Disable IR capture (release camera)'
            Write-Host '  3  Show status'
            Write-Host '  4  Install / repair virtual camera (UAC)'
            Write-Host '  5  Color [experimental - offline only]'
            Write-Host '  6  Build native app (developer tools needed)'
            Write-Host '  7  Uninstall UmbraCam (confirmation + UAC)'
            Write-Host '  0  Exit menu (does NOT stop an enabled camera)'
            Write-Host ''
            # ReadLine returns null on redirected EOF. Read-Host can return an
            # empty string repeatedly and otherwise spin forever in automation.
            if ([Console]::IsInputRedirected) { $choice = [Console]::ReadLine() }
            else { $choice = Read-Host 'Choose an option' }
            if ($null -eq $choice) { break }
            $selected = Get-UmbraMenuAction $choice
            if ($selected -eq 'Exit') { break }
            if ($selected -eq 'Invalid') { Write-Host 'Choose a number from 0 to 7.'; continue }
            try { Invoke-UmbraAction $selected }
            catch { Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red }
        } while ($true)
    }
} catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    $result = 1
} finally {
    if ($ownsMutex) { $mutex.ReleaseMutex() }
    if ($null -ne $mutex) { $mutex.Dispose() }
}
exit $result

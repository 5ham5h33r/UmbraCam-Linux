[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('ir-only','rgb-first','ir-first','rgb-release','faceauth-ir','faceauth-pair')][string]$Mode,
    [Parameter(Mandatory = $true)]
    [ValidateSet('dark-uncovered','bright-rgb-covered','bright-uncovered')][string]$Condition,
    [ValidateRange(1, 60)][int]$Seconds = 20
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$binary = Join-Path $repoRoot 'build\umbracam-startup-probe.exe'
if (-not (Test-Path -LiteralPath $binary)) { throw 'Build with scripts\build-startup-probe.cmd first.' }
if (Get-Process umbracam,umbracam-startup-probe,umbracam-faceauth-probe,umbracam-privacy-probe,probe-dark -ErrorAction SilentlyContinue) {
    throw 'A camera publisher or diagnostic is running. Close it before this test.'
}
$devices = @(Get-PnpDevice -PresentOnly | Where-Object {
    $_.InstanceId -like 'USB\VID_3277&PID_0060&MI_00\*' -or $_.InstanceId -like 'USB\VID_3277&PID_0060&MI_02\*'
})
if ($devices.Count -ne 2 -or @($devices | Where-Object { $_.Status -ne 'OK' -or [int]$_.Problem -ne 0 }).Count) {
    throw 'Both exact physical camera interfaces must be present and healthy; no device states will be changed.'
}
$logDirectory = Join-Path $repoRoot ('build\startup-' + $Mode + '-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $logDirectory
$devices | Select-Object FriendlyName,InstanceId,Status,Problem | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logDirectory 'before.json')
@{ mode = $Mode; condition = $Condition; seconds = $Seconds; timestamp = (Get-Date).ToString('o'); framesSaved = $false } |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logDirectory 'conditions.json')
$stdout = Join-Path $logDirectory 'probe.log'
$stderr = Join-Path $logDirectory 'error.log'
Write-Output "Statistics-only capture: mode=$Mode condition=$Condition logs=$logDirectory"
Write-Output 'This opens camera streams, but does not install drivers, write registry settings or issue explicit camera-control SETs.'
$probe = $null
$forced = $false
try {
    $probe = Start-Process -FilePath $binary -ArgumentList @("--$Mode", "$Seconds") -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    $null = $probe.Handle
    if (-not $probe.WaitForExit(($Seconds + 35) * 1000)) {
        $forced = $true
        throw 'Capture watchdog expired; terminating only the owned diagnostic. Normal cleanup is NOT confirmed.'
    }
    Get-Content -LiteralPath $stdout
    Get-Content -LiteralPath $stderr
    if ($probe.ExitCode -ne 0) { throw "Diagnostic exited with code $($probe.ExitCode); see $logDirectory" }
} finally {
    if ($probe) {
        if (-not $probe.HasExited) { $forced = $true; $probe.Kill(); $probe.WaitForExit() }
        $probe.Dispose()
    }
    $after = @(Get-PnpDevice -InstanceId $devices.InstanceId)
    $after | Select-Object FriendlyName,InstanceId,Status,Problem | ConvertTo-Json |
        Set-Content -LiteralPath (Join-Path $logDirectory 'after.json')
    Write-Output "Diagnostic exited; forcedTermination=$forced. Check STOP/CLOSE records and camera indicator; device health alone does not prove release."
    if ($after.Count -ne 2 -or @($after | Where-Object { $_.Status -ne 'OK' -or [int]$_.Problem -ne 0 }).Count) {
        throw 'Post-test camera health changed; stop further tests and inspect after.json.'
    }
}

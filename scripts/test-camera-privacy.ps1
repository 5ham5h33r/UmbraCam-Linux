[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('winrt', 'directshow', 'ks-properties', 'kernel')][string]$CapturePath,
    [Parameter(Mandatory = $true)]
    [ValidatePattern('^[a-zA-Z0-9_&\\-]+$')][string]$DeviceMatch,
    [ValidateRange(1, 60)][int]$Seconds = 45
)
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$probePath = Join-Path $repoRoot 'build\umbracam-privacy-probe.exe'
if (-not (Test-Path -LiteralPath $probePath)) { throw 'Build the diagnostic using scripts\build-privacy-probe.cmd first.' }
if (Get-Process umbracam,probe-dark,umbracam-privacy-probe -ErrorAction SilentlyContinue) {
    throw 'A camera publisher or diagnostic is running. Stop it before testing.'
}
$matched = @(Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId.IndexOf($DeviceMatch, [StringComparison]::OrdinalIgnoreCase) -ge 0 })
if ($matched.Count -ne 1 -or $matched[0].Status -ne 'OK' -or [int]$matched[0].Problem -ne 0) {
    throw 'DeviceMatch must identify exactly one present, enabled camera interface.'
}
if ($matched[0].Class -ne 'Camera' -and $matched[0].Class -ne 'Image') { throw 'Matched device is not a camera.' }
$logDirectory = Join-Path $repoRoot ('build\privacy-' + $CapturePath + '-' + [guid]::NewGuid().ToString('N'))
$null = New-Item -ItemType Directory -Path $logDirectory
$matched | Select-Object FriendlyName,InstanceId,Status,Problem | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logDirectory 'before.json')
$stdout = Join-Path $logDirectory 'probe.log'
$stderr = Join-Path $logDirectory 'probe.err'
Write-Output "Statistics-only test logs: $logDirectory"
Write-Output 'No drivers, registry settings or device enable/disable states will be changed.'
$probe = $null
try {
    $probe = Start-Process -FilePath $probePath -ArgumentList @("--$CapturePath", '--device-match', $DeviceMatch, "$Seconds") -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr -PassThru
    $null = $probe.Handle
    if (-not $probe.WaitForExit(($Seconds + 20) * 1000)) { throw 'Capture watchdog expired; terminating only this diagnostic.' }
    Get-Content -LiteralPath $stdout
    Get-Content -LiteralPath $stderr
    if ($probe.ExitCode -ne 0) { throw "Diagnostic exited with code $($probe.ExitCode). See $logDirectory" }
} finally {
    if ($probe) {
        if (-not $probe.HasExited) { $probe.Kill(); $probe.WaitForExit() }
        $probe.Dispose()
    }
    Get-PnpDevice -InstanceId $matched[0].InstanceId | Select-Object FriendlyName,InstanceId,Status,Problem |
        ConvertTo-Json | Set-Content -LiteralPath (Join-Path $logDirectory 'after.json')
    Write-Output 'Diagnostic process exited. Device settings were left unchanged.'
}

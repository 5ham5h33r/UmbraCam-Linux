# Offline validation only: never invoke a valid camera capture command.
$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$runner = Join-Path $repoRoot 'scripts\test-camera-privacy.ps1'
$binary = Join-Path $repoRoot 'build\umbracam-privacy-probe.exe'
$statisticsTest = Join-Path $repoRoot 'build\frame-statistics-test.exe'
if (-not (Test-Path -LiteralPath $binary) -or -not (Test-Path -LiteralPath $statisticsTest)) {
    throw 'Run scripts\build-privacy-probe.cmd first.'
}
$tokens = $null
$errors = $null
[System.Management.Automation.Language.Parser]::ParseFile($runner, [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count) { throw ($errors | Out-String) }
foreach ($invalid in @(
    @{ CapturePath = 'kernel'; DeviceMatch = 'VID_TEST'; Seconds = 0 },
    @{ CapturePath = 'kernel'; DeviceMatch = 'VID_TEST'; Seconds = 61 },
    @{ CapturePath = 'unknown'; DeviceMatch = 'VID_TEST'; Seconds = 1 },
    @{ CapturePath = 'kernel'; DeviceMatch = 'bad";input'; Seconds = 1 }
)) {
    $rejected = $false
    try { & $runner @invalid } catch {
        if ($_.FullyQualifiedErrorId -notlike 'ParameterArgumentValidationError*') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'Invalid runner input was accepted.' }
}
& $binary --invalid
if ($LASTEXITCODE -ne 2) { throw 'Invalid binary arguments were accepted.' }
& $binary --kernel --device-match VID_TEST 0
if ($LASTEXITCODE -ne 2) { throw 'Invalid binary duration was accepted.' }
& $statisticsTest
if ($LASTEXITCODE -ne 0) { throw 'Frame-statistics test failed.' }
Write-Output 'PASS: runner syntax, four parameter guards, two CLI guards, and Gray8 statistics tests. No camera opened.'

# Offline checks: no valid invocation of either camera binary.
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$runner = Join-Path $root 'scripts\test-camera-startup.ps1'
$tokens = $null
$errors = $null
[System.Management.Automation.Language.Parser]::ParseFile($runner, [ref]$tokens, [ref]$errors) | Out-Null
if ($errors.Count) { throw ($errors | Out-String) }
foreach ($invalid in @(
    @{ Mode='bad'; Condition='bright-uncovered'; Seconds=1 },
    @{ Mode='ir-only'; Condition='unknown'; Seconds=1 },
    @{ Mode='ir-only'; Condition='dark-uncovered'; Seconds=0 },
    @{ Mode='ir-only'; Condition='dark-uncovered'; Seconds=61 }
)) {
    $rejected = $false
    try { & $runner @invalid } catch {
        if ($_.FullyQualifiedErrorId -notlike 'ParameterArgumentValidationError*') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'Runner accepted invalid input.' }
}
$cases = @{
    'umbracam-camera-profiles.exe' = @('', '--invalid', '--inventory extra')
    'umbracam-startup-probe.exe' = @('', '--invalid 1', '--ir-only', '--ir-only 0', '--ir-only 61',
        '--ir-only -1', '--ir-only 1.0', '--ir-only 1x', '--ir-only 999999999999999999999',
        '--ir-only 1 extra', '--faceauth-ir 0', '--faceauth-pair 61', '--rgb-release +1', '--faceauth-group 0')
}
$count = 0
foreach ($binaryName in $cases.Keys) {
    $binary = Join-Path $root "build\$binaryName"
    if (-not (Test-Path -LiteralPath $binary)) { throw 'Run scripts\build-startup-probe.cmd first.' }
    foreach ($arguments in $cases[$binaryName]) {
        $info = New-Object System.Diagnostics.ProcessStartInfo
        $info.FileName = $binary
        $info.Arguments = $arguments
        $info.UseShellExecute = $false
        $info.CreateNoWindow = $true
        $process = [System.Diagnostics.Process]::Start($info)
        try {
            if (-not $process.WaitForExit(5000)) { $process.Kill(); $process.WaitForExit(); throw "Guard timed out: $arguments" }
            if ($process.ExitCode -ne 2) { throw "Expected exit 2: $binaryName $arguments" }
        } finally { $process.Dispose() }
        ++$count
    }
}
Write-Output "PASS: runner syntax, four runner guards, $count executable guards. No camera opened."

# Offline checks only. Every invocation is invalid before hardware initialization.
$ErrorActionPreference = 'Stop'
$binary = Join-Path (Split-Path -Parent $PSScriptRoot) 'build\umbracam-faceauth-probe.exe'
if (-not (Test-Path -LiteralPath $binary)) { throw 'Run scripts\build-faceauth-probe.cmd first.' }
foreach ($arguments in @('', '--invalid', '--prestart extra', '--query extra', '--restore-general extra')) {
    $startInfo = New-Object System.Diagnostics.ProcessStartInfo
    $startInfo.FileName = $binary
    $startInfo.Arguments = $arguments
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $process = [System.Diagnostics.Process]::Start($startInfo)
    try {
        if (-not $process.WaitForExit(5000)) {
            $process.Kill()
            $process.WaitForExit()
            throw "Argument guard timed out: $arguments"
        }
        if ($process.ExitCode -ne 2) { throw "Expected exit 2 for '$arguments', got $($process.ExitCode)." }
    } finally { $process.Dispose() }
}
Write-Output 'PASS: five FaceAuth CLI guards. No camera opened.'

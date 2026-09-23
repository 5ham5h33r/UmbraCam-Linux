[CmdletBinding()]
param([ValidateSet('normal', 'virtual', 'component')][string]$Camera = 'virtual')

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$testExe = Join-Path $root 'build\umbracam-stream-test.exe'
$runId = [guid]::NewGuid().ToString('N')
$stdoutPath = Join-Path $root "build\stream-$Camera-$runId.log"
$stderrPath = Join-Path $root "build\stream-$Camera-$runId.err"
$streamProcess = Start-Process -FilePath $testExe -ArgumentList "--$Camera" -WindowStyle Hidden -RedirectStandardOutput $stdoutPath -RedirectStandardError $stderrPath -PassThru
$null = $streamProcess.Handle
try {
    if (-not $streamProcess.WaitForExit(20000)) {
        $streamProcess.Kill()
        $streamProcess.WaitForExit()
        throw "The $Camera camera stream timed out after 20 seconds. Test process stopped."
    }
    if ($streamProcess.ExitCode -ne 0) {
        throw "The $Camera camera stream test failed (exit $($streamProcess.ExitCode))."
    }
} finally {
    if (-not $streamProcess.HasExited) {
        $streamProcess.Kill()
        $streamProcess.WaitForExit()
    }
    if (Test-Path -LiteralPath $stdoutPath) { Get-Content -LiteralPath $stdoutPath | Write-Host }
    if (Test-Path -LiteralPath $stderrPath) { Get-Content -LiteralPath $stderrPath | Write-Host }
    $streamProcess.Dispose()
}

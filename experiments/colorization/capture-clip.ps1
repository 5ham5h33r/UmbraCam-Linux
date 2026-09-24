param([Parameter(Mandatory = $true)][string]$OutputDirectory)
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$binary = Join-Path $root 'build\capture-clip.exe'
if (-not (Test-Path -LiteralPath $binary)) { throw 'Build the capture helper first.' }
if (Get-Process umbracam -ErrorAction SilentlyContinue) {
    throw 'Stop UmbraCam before recording this offline experiment.'
}
$target = [IO.Path]::GetFullPath($OutputDirectory)
if ($target.Contains('"')) { throw 'Invalid output path.' }
if (Test-Path -LiteralPath $target) { throw 'Choose a new output directory.' }
$stdout = Join-Path $root ('build\capture-' + [guid]::NewGuid().ToString('N') + '.log')
$stderr = $stdout + '.err'
$child = $null
try {
    $child = Start-Process -FilePath $binary -ArgumentList ('"' + $target + '"') `
        -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $null = $child.Handle # Preserve ExitCode after WaitForExit in Windows PowerShell.
    if (-not $child.WaitForExit(25000)) { throw 'IR capture exceeded the 25-second timeout.' }
    Get-Content -LiteralPath $stdout
    Get-Content -LiteralPath $stderr
    if ($child.ExitCode -ne 0) { throw "IR capture failed with exit code $($child.ExitCode)." }
} finally {
    if ($child) {
        if (-not $child.HasExited) { $child.Kill(); $child.WaitForExit() }
        $child.Dispose()
    }
}

# No Pester dependency, camera access, administrator prompts or process changes.
$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) 'scripts\control-functions.ps1')
$script:Passed = 0
function Assert-True($Value, [string]$Message) { if (-not $Value) { throw $Message } }
function Assert-Throws([scriptblock]$Operation, [string]$Pattern) {
    try { & $Operation } catch {
        if ($_.Exception.Message -notmatch $Pattern) { throw "Unexpected error: $_" }
        return
    }
    throw 'Expected operation to fail.'
}
function Test-Case([string]$Name, [scriptblock]$Body) {
    # These safe defaults also make accidental side effects fail the test.
    function Get-UmbraPublishers { @() }
    function Confirm-UmbraAction { throw 'Unexpected confirmation' }
    function Read-Host { throw 'Unexpected prompt' }
    function Invoke-UmbraScript { throw 'Unexpected child script' }
    function Start-UmbraPublisher { throw 'Unexpected camera start' }
    function Send-UmbraStop { throw 'Unexpected stop signal' }
    function Wait-UmbraStopped { throw 'Unexpected process wait' }
    function Get-UmbraRegistration { 'Installed' }
    function Test-UmbraBuild { $true }
    $script:Calls = @()
    & $Body
    $script:Passed++
    Write-Host "PASS: $Name"
}

Test-Case 'Menu maps all choices and rejects unknown input' {
    $expected = @('Exit', 'Enable', 'Disable', 'Status', 'Install', 'Color', 'Build', 'Uninstall')
    for ($i = 0; $i -lt $expected.Count; $i++) {
        Assert-True ((Get-UmbraMenuAction " $i ") -eq $expected[$i]) "Bad option $i"
    }
    Assert-True ((Get-UmbraMenuAction 'bad') -eq 'Invalid') 'Invalid option accepted'
    Assert-Throws { Invoke-UmbraAction 'bad' } 'Unknown'
}
Test-Case 'Enable does not duplicate an existing publisher' {
    function Get-UmbraPublishers { [pscustomobject]@{ Id = 123 } }
    Enable-Umbra
}
Test-Case 'Enable installed camera launches publisher' {
    function Start-UmbraPublisher { $script:Calls += 'start' }
    Enable-Umbra
    Assert-True ($script:Calls -join ',' -eq 'start') 'Publisher not started exactly once'
}
Test-Case 'Declining installation never opens camera' {
    function Get-UmbraRegistration { 'Not installed' }
    function Confirm-UmbraAction { $false }
    Enable-Umbra
}
Test-Case 'First run installs before starting' {
    function Get-UmbraRegistration { if ('install.ps1' -in $script:Calls) { 'Installed' } else { 'Not installed' } }
    function Confirm-UmbraAction { $true }
    function Invoke-UmbraScript($Name) { $script:Calls += $Name }
    function Start-UmbraPublisher { $script:Calls += 'start' }
    Enable-Umbra
    Assert-True ($script:Calls -join ',' -eq 'install.ps1,start') 'Wrong setup order'
}
Test-Case 'Failed setup never launches publisher' {
    function Get-UmbraRegistration { 'Not installed' }
    function Confirm-UmbraAction { $true }
    function Invoke-UmbraScript { throw 'UAC cancelled' }
    Assert-Throws { Enable-Umbra } 'UAC cancelled'
}
Test-Case 'Failed verification never launches publisher' {
    function Get-UmbraRegistration { 'Not installed' }
    function Confirm-UmbraAction { $true }
    function Invoke-UmbraScript { }
    Assert-Throws { Enable-Umbra } 'not found'
}
Test-Case 'Enumeration failure is not mistaken for missing installation' {
    function Get-UmbraRegistration { throw 'Enumeration failure' }
    Assert-Throws { Enable-Umbra } 'Enumeration failure'
}
Test-Case 'Missing binaries can be declined without setup' {
    function Test-UmbraBuild { $false }
    function Confirm-UmbraAction { $false }
    Assert-Throws { Enable-Umbra } 'No changes made'
}
Test-Case 'Missing binaries build only after confirmation' {
    function Test-UmbraBuild { 'build-native.ps1' -in $script:Calls }
    function Confirm-UmbraAction { $true }
    function Invoke-UmbraScript($Name) { $script:Calls += $Name }
    Require-UmbraBuild
    Assert-True ($script:Calls -join ',' -eq 'build-native.ps1') 'Build not invoked'
}
Test-Case 'Build refuses a running publisher' {
    function Get-UmbraPublishers { [pscustomobject]@{ Id = 123 } }
    Assert-Throws { Build-Umbra } 'Disable'
}
Test-Case 'Disable is idempotent and does not need a build' {
    function Test-UmbraBuild { throw 'Disable must not require build files' }
    Disable-Umbra
}
Test-Case 'Disable signals and verifies shutdown without build files' {
    function Get-UmbraPublishers { [pscustomobject]@{ Id = 123 } }
    function Test-UmbraBuild { throw 'Disable must not require build files' }
    function Send-UmbraStop { $script:Calls += 'signal'; $true }
    function Wait-UmbraStopped { $script:Calls += 'wait'; $true }
    Disable-Umbra
    Assert-True ($script:Calls -join ',' -eq 'signal,wait') 'Stop was not verified'
}
Test-Case 'Stop timeout is reported, not called success' {
    function Get-UmbraPublishers { [pscustomobject]@{ Id = 123 } }
    function Send-UmbraStop { $true }
    function Wait-UmbraStopped { $false }
    Assert-Throws { Disable-Umbra } 'did not exit'
}
Test-Case 'Unavailable stop event fails safely' {
    function Get-UmbraPublishers { [pscustomobject]@{ Id = 123 } }
    function Send-UmbraStop { $false }
    Assert-Throws { Disable-Umbra } 'stop event is unavailable'
}
Test-Case 'Uninstall requires exact confirmation' {
    function Read-Host { 'no' }
    Uninstall-Umbra
}
Test-Case 'Confirmed uninstall stops capture before removal and verifies' {
    function Read-Host { 'UNINSTALL' }
    function Disable-Umbra { $script:Calls += 'disable' }
    function Invoke-UmbraScript($Name) { $script:Calls += $Name }
    function Get-UmbraRegistration { 'Not installed' }
    Uninstall-Umbra
    Assert-True ($script:Calls -join ',' -eq 'disable,uninstall.ps1') 'Wrong uninstall order'
}
Test-Case 'Uninstall aborts when capture cannot stop' {
    function Read-Host { 'UNINSTALL' }
    function Disable-Umbra { throw 'Stop timeout' }
    Assert-Throws { Uninstall-Umbra } 'Stop timeout'
}
Test-Case 'Uninstall cannot silently claim removal failed to verify' {
    function Read-Host { 'UNINSTALL' }
    function Invoke-UmbraScript { }
    Assert-Throws { Uninstall-Umbra } 'removal could not be verified'
}
Test-Case 'Experimental color is informational and never starts a camera' { Show-UmbraColor }
Test-Case 'Status does not open a stream' { Show-UmbraStatus }
Write-Host "All $script:Passed launcher tests passed. No real camera or setup actions executed."

[CmdletBinding()]
param(
    [switch] $ListGroups
)

$ErrorActionPreference = 'Stop'

# Windows PowerShell needs this assembly to convert WinRT asynchronous
# operations into .NET Tasks.
Add-Type -AssemblyName System.Runtime.WindowsRuntime | Out-Null

function Get-AsyncResult {
    param(
        [Parameter(Mandatory)] $Operation,
        [Parameter(Mandatory)] [Type] $ResultType
    )

    $method = [System.WindowsRuntimeSystemExtensions].GetMethods() |
        Where-Object {
            $_.Name -eq 'AsTask' -and
            $_.GetParameters().Count -eq 1 -and
            $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncOperation`1'
        } |
        Select-Object -First 1

    $task = $method.MakeGenericMethod($ResultType).Invoke($null, @($Operation))
    $task.Wait(-1) | Out-Null
    return $task.Result
}

function Complete-AsyncAction {
    param([Parameter(Mandatory)] $Operation)

    $method = [System.WindowsRuntimeSystemExtensions].GetMethods() |
        Where-Object {
            $_.Name -eq 'AsTask' -and
            $_.GetParameters().Count -eq 1 -and
            $_.GetParameters()[0].ParameterType.Name -eq 'IAsyncAction'
        } |
        Select-Object -First 1

    $task = $method.Invoke($null, @($Operation))
    $task.Wait(-1) | Out-Null
}

function Get-DeepestException {
    param([Parameter(Mandatory)] [Exception] $Exception)

    $current = $Exception
    while ($null -ne $current.InnerException) {
        $current = $current.InnerException
    }
    return $current
}

[void][Windows.Media.Capture.MediaCapture, Windows.Media.Capture, ContentType = WindowsRuntime]
[void][Windows.Media.Capture.MediaCaptureInitializationSettings, Windows.Media.Capture, ContentType = WindowsRuntime]
[void][Windows.Media.Capture.Frames.MediaFrameSourceGroup, Windows.Media.Capture, ContentType = WindowsRuntime]
[void][Windows.Media.Capture.Frames.MediaFrameReader, Windows.Media.Capture, ContentType = WindowsRuntime]

$infrared = [Windows.Media.Capture.Frames.MediaFrameSourceKind]::Infrared
$groups = Get-AsyncResult `
    ([Windows.Media.Capture.Frames.MediaFrameSourceGroup]::FindAllAsync()) `
    ([System.Collections.Generic.IReadOnlyList[Windows.Media.Capture.Frames.MediaFrameSourceGroup]])

if ($ListGroups) {
    foreach ($candidate in $groups) {
        [pscustomobject]@{
            GroupName = $candidate.DisplayName
            GroupId = $candidate.Id
            Sources = @($candidate.SourceInfos | ForEach-Object {
                "$($_.SourceKind) | $($_.Id) | $($_.DeviceInformation.Name) | $($_.DeviceInformation.Id)"
            }) -join "`n"
        } | Format-List
    }
    return
}

# Some Windows Hello drivers publish both a composite Color+IR group and an
# IR-only group. The composite group can advertise IR yet expose no frames to
# ordinary MediaCapture clients; prefer the dedicated IR group when present.
$group = $groups | Where-Object {
    $sources = @($_.SourceInfos)
    $sources.Count -gt 0 -and
    @($sources | Where-Object SourceKind -eq $infrared).Count -gt 0 -and
    @($sources | Where-Object SourceKind -ne $infrared).Count -eq 0
} | Select-Object -First 1

if ($null -eq $group) {
    $group = $groups | Where-Object {
        $_.SourceInfos | Where-Object SourceKind -eq $infrared
    } | Select-Object -First 1
}

if ($null -eq $group) {
    throw 'No Media Foundation infrared source was found. Confirm the Windows Hello IR driver is enabled.'
}

$settings = [Windows.Media.Capture.MediaCaptureInitializationSettings]::new()
$settings.SourceGroup = $group
$settings.StreamingCaptureMode = [Windows.Media.Capture.StreamingCaptureMode]::Video
$settings.MemoryPreference = [Windows.Media.Capture.MediaCaptureMemoryPreference]::Cpu
$settings.SharingMode = [Windows.Media.Capture.MediaCaptureSharingMode]::SharedReadOnly

$capture = [Windows.Media.Capture.MediaCapture]::new()
$reader = $null
$frame = $null

try {
    Complete-AsyncAction ($capture.InitializeAsync($settings))

    $source = $capture.FrameSources.Values |
        Where-Object { $_.Info.SourceKind -eq $infrared } |
        Select-Object -First 1
    if ($null -eq $source) {
        $available = @($capture.FrameSources.Values | ForEach-Object {
            "$($_.Info.SourceKind):$($_.Info.Id)"
        }) -join ', '
        throw "The '$($group.DisplayName)' group opened but did not expose an infrared source. Available sources: $available"
    }

    $reader = Get-AsyncResult `
        ($capture.CreateFrameReaderAsync($source)) `
        ([Windows.Media.Capture.Frames.MediaFrameReader])
    $status = Get-AsyncResult `
        ($reader.StartAsync()) `
        ([Windows.Media.Capture.Frames.MediaFrameReaderStartStatus])
    if ($status -ne [Windows.Media.Capture.Frames.MediaFrameReaderStartStatus]::Success) {
        throw "IR frame reader did not start successfully: $status"
    }

    Start-Sleep -Milliseconds 750
    $frame = $reader.TryAcquireLatestFrame()
    if ($null -eq $frame -or $null -eq $frame.VideoMediaFrame -or $null -eq $frame.VideoMediaFrame.SoftwareBitmap) {
        throw 'The IR source started but produced no CPU-readable video frame.'
    }

    $bitmap = $frame.VideoMediaFrame.SoftwareBitmap
    [pscustomobject]@{
        SourceGroup = $group.DisplayName
        SourceId = $source.Info.Id
        Width = $bitmap.PixelWidth
        Height = $bitmap.PixelHeight
        PixelFormat = $bitmap.BitmapPixelFormat
        AlphaMode = $bitmap.BitmapAlphaMode
        Probe = 'PASS'
    } | Format-List
}
catch {
    $cause = Get-DeepestException $_.Exception
    $code = '0x{0:X8}' -f ($cause.HResult -band 0xffffffff)
    $hint = if ($cause.HResult -eq [int]0x80070005) {
        'Enable Camera access and Let desktop apps access your camera in Settings > Privacy & security > Camera, then run the probe again.'
    } else {
        'Close any app holding the IR camera and retry. Restart Windows if the camera was just updated.'
    }
    throw "IR probe failed ($code): $($cause.Message) $hint"
}
finally {
    if ($null -ne $frame) { $frame.Dispose() }
    if ($null -ne $reader) {
        try { Complete-AsyncAction ($reader.StopAsync()) } catch { Write-Verbose $_ }
        $reader.Dispose()
    }
    if ($null -ne $capture) { $capture.Dispose() }
}

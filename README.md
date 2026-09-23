# UmbraCam

`UmbraCam` makes a Windows Hello infrared sensor available to Google Meet,
Zoom, Teams, and other webcam clients as a dedicated **UmbraCam IR Camera**
device.

It does not alter the laptop's OEM camera driver and does not use OBS. The physical
IR sensor remains a Windows Hello sensor camera; a native Windows 11 Media
Foundation virtual-camera source publishes a separate, standard camera device.

## Status

The project is being developed in small, reviewable feature branches. It now
contains the native virtual-camera foundation and a consent-owning IR publisher.

## Architecture

```text
Windows Hello IR camera
        |  WinRT MediaFrameReader, SourceKind=Infrared
        v
UmbraCam publisher
        |  grayscale conversion
        v
Native Media Foundation virtual camera
        v
UmbraCam IR Camera (Google Meet / Zoom / Teams camera list)
```

## Prerequisites

- Windows 11 22H2 or newer (the native virtual-camera API requires Windows 11)
- Windows camera privacy access enabled for desktop apps
- Visual Studio 2022 Build Tools with the Desktop C++ workload (required for
  the native virtual-camera DLL)

## Development checks

Build first, then run the non-installing hardware probe:

```powershell
.\scripts\build-native.ps1
.\scripts\probe-ir.ps1
```

The probe opens the first Media Foundation infrared source, reads one frame,
prints its dimensions, then closes it. It creates no virtual device and changes
no driver settings. It uses the exact native MTA and Direct3D-surface conversion
path as the publisher; this is required by newer Realtek Windows Hello stacks.

After installation, verify that the standard virtual camera is available to
camera clients with `build\umbracam-probe.exe --check-virtual-camera`.

## Build and run

Build from Windows PowerShell, then run the installer. It displays the normal
Windows UAC prompt because the Frame Server needs a machine-wide registration:

```powershell
.\scripts\build-native.ps1
.\scripts\install.ps1
.\build\umbracam.exe
```

On first launch, the bridge requests camera access by initializing
`MediaCapture` from its STA UI thread. Windows can show its consent UI when the
system supports per-app consent. For an unpackaged desktop app, Windows may
instead rely on the system-wide **Let desktop apps access your camera** switch.
If access is blocked, UmbraCam displays its own explanation and opens the
Camera privacy settings page; it never changes a Windows privacy setting itself.

Once UmbraCam is installed, **UmbraCam IR Camera** is a normal camera option
alongside the laptop's FHD camera. Launch the publisher before a call so it
can provide IR frames, then select **UmbraCam IR Camera** in Google Meet.
Stop the publisher after the call with:

```powershell
.\build\umbracam.exe --stop
```

To remove the registration later, run `.\scripts\uninstall.ps1`; it also asks
for UAC consent.

## Safety

Do **not** replace the IR camera's OEM driver with a generic UVC driver or
change its `SensorCameraMode` / `SkipCameraEnumeration` settings. Those are
the settings that keep the sensor usable for Windows Hello.

## Validation boundary

The build and source-discovery checks can run from any developer account. The
final frame check must run in the same interactive Windows account that will
use Meet, Zoom, or Teams, because Windows applies camera privacy and sensor
access by user session. Run `scripts\probe-ir.ps1` there before installing.

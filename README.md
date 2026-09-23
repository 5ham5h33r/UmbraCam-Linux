# G14 IR Camera Bridge

`G14 IR Camera Bridge` will make the Windows Hello infrared sensor on an ASUS
ROG Zephyrus G14 available to Google Meet, Zoom, Teams, and other webcam
clients as a dedicated **G14 IR Camera** device.

It does not alter the ASUS camera driver and does not use OBS.  The physical
IR sensor remains a Windows Hello sensor camera; a native Windows 11 Media
Foundation virtual-camera source publishes a separate, standard camera device.

## Status

The project is being developed in small, reviewable feature branches. The
first feature is an IR capture probe that verifies the Media Foundation source
and captures one frame before virtual-camera work begins.

## Architecture

```text
USB2.0 IR UVC WebCam (Windows Hello sensor)
        |  WinRT MediaFrameReader, SourceKind=Infrared
        v
G14 IR Camera Bridge publisher
        |  grayscale conversion and de-flicker
        v
Native Media Foundation virtual camera
        v
G14 IR Camera (Google Meet / Zoom / Teams camera list)
```

## Prerequisites

- Windows 11 22H2 or newer (the native virtual-camera API requires Windows 11)
- Windows camera privacy access enabled for desktop apps
- Visual Studio 2022 Build Tools with the Desktop C++ workload (required for
  the native virtual-camera DLL)

## Development checks

Run the non-installing source probe from Windows PowerShell 5.1:

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\probe-ir.ps1
```

The probe opens the first Media Foundation infrared source, reads one frame,
prints its dimensions and pixel format, then closes it. It creates no virtual
device and changes no driver settings.

## Safety

Do **not** replace the IR camera's OEM driver with a generic UVC driver or
change its `SensorCameraMode` / `SkipCameraEnumeration` settings. Those are
the settings that keep the sensor usable for Windows Hello.

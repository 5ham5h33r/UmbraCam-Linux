# Camera privacy and capture-path diagnostics

These opt-in developer tests investigate an IR stream that turns into a uniform
image in darkness. They do not install anything, modify camera drivers or registry
values, toggle devices, change exposure/illumination settings, or save images.
They do open the selected camera, and its activity indicator can turn on.

See the [2026-09-27 hardware results](privacy-test-2026-09-27.md) for measured
results and limitations; these tools are not a proven dark-room workaround.

The separate [2026-09-28 USB trace controls](usb-camera-trace-2026-09-28.md)
compare covered and bright-room conditions. Unlike these statistics-only probes,
those opt-in USB recordings contain image payloads and require an additional
capture driver. They are not an uncovered dark-room reproduction.

The [camera-control research](camera-control-research-2026-09-28.md) records
confirmed FaceAuth capabilities, incompatible workaround leads, and the proposed
controlled illumination-mode comparison. No dark-room fix is verified yet.

Close UmbraCam and camera-using applications first. Build from a Developer Command
Prompt, or use the supplied script with Visual Studio 2022 C++ Build Tools and
Windows SDK 10.0.26100.0 installed:

```powershell
cmd /c scripts\build-privacy-probe.cmd
```

The build also runs the Gray8 statistics tests (padding, nonuniform pixels, and
invalid/overflowing bounds). Run the hardware probe as the signed-in desktop user,
not as Administrator. Substitute a unique substring of the physical IR device's
Device Manager instance ID; do not copy another machine's instance ID blindly:

```powershell
.\scripts\test-camera-privacy.ps1 -CapturePath winrt -DeviceMatch 'VID_3277&PID_0060&MI_02' -Seconds 30
.\scripts\test-camera-privacy.ps1 -CapturePath kernel -DeviceMatch 'VID_3277&PID_0060&MI_02' -Seconds 30
```

Each command creates a separate ignored `build/privacy-...` folder with statistics,
errors and device-state snapshots. It preserves the device's enabled/disabled state.
The runner enforces a duration-plus-20-second watchdog and terminates only its own
diagnostic process if necessary. Camera initialization/shutdown can add time to the
requested capture duration. No frames are written to disk or sent over a network.

## Paths and interpretation

- `winrt`: selects the matching **IR-only** MediaFrameSourceGroup, not an aggregate
  RGB+IR processing group. Reads native Gray8 pixels and polls CameraOcclusionInfo
  while streaming. Reports support flags and API failures separately from state.
- `kernel`: enumerates the camera's advertised native Gray8/L8-IR kernel pin formats,
  negotiates a supported format and reads samples through IOCTL_KS_READ_STREAM.
  It does not use MediaCapture, a Media Foundation graph, or KSProxy. It starts and
  stops only its own streaming pin, and reads KSPROPERTY_CAMERACONTROL_PRIVACY;
  it never writes that privacy property. This still uses the installed kernel
  driver stack, so it is **not a raw USB or firmware-only measurement**.
- `directshow`: independently tries a KSProxy -> Sample Grabber -> Null Renderer
  graph with direct connections and no inserted conversion filters. Some Hello
  sensor interfaces cannot bind to KSProxy. Failure to open is an inconclusive
  capture test, not evidence of gray frames or a hardware defect.
- `ks-properties`: reads the kernel privacy property and pin count without starting
  a stream. Idle privacy values are not authoritative; prefer the streaming test.

`uniform` counts frames whose visible pixels all have the same value, excluding
row padding. A nonuniform frame is not automatically a usable face image: it can
also be noise or an unilluminated exposure. Statistics alone cannot verify image
quality, identity, temporal flicker, or suitability for a video call.

A shutter report says what the camera stack **believes**, not whether a physical
shutter really exists or is closed. Unsupported reporting is not proof that it is
open. Compare the same lighting and physical lens coverage across paths. A
successful lower-level stream does not establish a production-ready workaround.

## References

- [CameraOcclusionInfo and streaming-state caveats](https://learn.microsoft.com/en-us/uwp/api/windows.media.devices.cameraocclusioninfo)
- [Privacy controls, false dark-room detection, and activity LEDs](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/camera-privacy-controls)
- [Windows privacy-shutter image replacement](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/privacy-shutter-replacement-image)
- [KSPROPERTY_CAMERACONTROL_PRIVACY](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/ksproperty-cameracontrol-privacy)
- [Kernel Streaming proxy architecture](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/kernel-streaming-proxy-plug-ins-design-guide)
- [Kernel pin format negotiation](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/ksproperty-pin-dataintersection)
- [Kernel stream reads](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ks/ni-ks-ioctl_ks_read_stream)

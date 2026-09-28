# Camera control and firmware research

## Status

No complete dark-room fix is verified. A successful read-only query confirmed
alternating IR illumination support. The subsequent covered-RGB comparison
below successfully selected this mode but did not stop privacy blanking.
This control is not a demonstrated privacy override.

Target: ASUS GA403WR, physical camera USB VID 3277 / PID 0060, IR interface MI_02.
This report extends the [USB trace findings](usb-camera-trace-2026-09-28.md).
The earlier uncovered dark-room kernel test and the later covered USB test are
different experiments; the latter is not relabeled as an uncovered dark test.

## Successful capability query

The following API path worked without starting video or explicitly setting any
camera property:

1. Open the exact IR sensor symbolic link with `MFCreateDeviceSource`.
2. Query its `IMFGetService` and request `IMFExtendedCameraController` using
   service GUID `GUID_NULL`.
3. Call `GetExtendedCameraControl(0, 35, ...)`: stream index 0, FaceAuth property.
4. Read `GetFlags()` and `GetCapabilities()`, then shut down the source.

Observed on this machine:

```text
MF_EXTENDED_CONTROLLER hr=0x00000000
MF_EXTENDED_FACEAUTH stream=0 hr=0x00000000 flags=0x1 capabilities=0x3
MF_EXTENDED_FACEAUTH stream=1 hr=0xc00d36b3
MF_EXTENDED_FACEAUTH stream=4294967295 hr=0x80070057
```

Microsoft defines flag `0x1` as FaceAuth disabled/general-purpose IR and `0x2` as
alternating illuminated/unilluminated frames. Flag `0x4` is background subtraction.
Thus the advertised modes are general-purpose and alternating illumination;
background subtraction is not advertised. The observed current setting was
general-purpose **while idle**. It does not establish the mode during capture.

The existing covered and bright USB traces each contain one FaceAuth SET at
shutdown: selector 6, unit 14, control interface 2, payload
`01 03 01 00 00 00 00 00 00` (one entry, stream interface 3, mode 1).
No other matching FaceAuth setup request was found in either saved file.
These recordings began after boot/enumeration and have a forced-stop tail
limitation. Absence of an earlier mode change in them does not prove that the
camera never used alternating mode or what its initial state was.

### Failed query paths are not missing capabilities

Direct KS and `IKsControl` GETs with a manually constructed extended-property
payload returned error 87 / `0x80070057` for FaceAuth. The size-only query
reported 40 bytes, whereas the current driver documentation specifies a 64-byte
header-plus-video-proc payload. Both sizes failed in the manual query. Actual
KS pins 0/1, filter scope, and the USB stream identifier 3 were tried, with no
successful manual GET. The high-level API succeeding means these failures
must not be treated as proof that FaceAuth is unsupported or the device broken.
The exact manual-query discrepancy is unresolved.

The IR filter exposes five topology nodes, including three device-specific
nodes. Direct node GETs for the Microsoft XU FaceAuth selector returned
1168/1170, not a usable raw control value.

## Descriptor compatibility matters

From the saved USB configuration descriptor:

| Interface | Extension unit | Advertised selectors | Interpretation |
| --- | --- | --- | --- |
| IR (2) | Microsoft `0f3f95dc-2632-4c4e-92c9-a04782f43bc8`, unit 14 | 6, 9 | FaceAuth and metadata; no selector 10 IR torch |
| IR (2) | `1229a78c-47b4-4094-b0ce-db07386fb938`, unit 10 | 10, 11 | Vendor controls; no validated privacy-override semantics |
| IR (2) | `45b5da73-23c1-4a3d-a368-610f078c4397`, unit 11 | none | No advertised controls |
| RGB (0) | `26b8105a-0713-4870-979d-da79444bb68e`, unit 7 | 13 | Not the IR selector 14 used by an older Lenovo workaround |

IR-torch property 38 returned error 1168 (`ERROR_NOT_FOUND`) in direct GETs,
consistent with the missing torch selector. Do not transfer a different
camera's emitter/register command merely because one extension GUID matches.

## Firmware and workaround leads checked

- The [ASUS GA403WR download page](https://www.asus.com/me-en/supportonly/ga403wr/helpdesk_download/)
  lists Realtek Camera_Extension 10.0.22000.10006, not a camera-firmware fix.
  The audited installed RGB extension is newer, 10.0.22000.10010, and changes
  color-format metadata. No compatible camera-firmware remedy was found in
  the public sources checked. This is not proof that ASUS has no internal fix.
- Installed BIOS version is GA403WR.310. ASUS also lists version 310; the
  installed SMBIOS date and published listing date differ. Matching version
  numbers do not prove identical firmware bytes. No camera-specific fix was
  identified in its public description.
- A real [ASUS ExpertBook camera-firmware project](https://github.com/burakgon/asus-expertbook-linux/tree/main/camera-firmware)
  documents package 3009, but explicitly checks B9406CAA and an exact ESRT GUID.
  It is not a GA403WR package. Nothing from it was executed or flashed.
- [Lenovo's published EnableCamera.reg](https://download.lenovo.com/km/media/attachment/EnableCamera.reg)
  was downloaded as text only. It sets `CAMERA_PRIVACY` in a Realtek DMFT
  per-device saved-property path for `VID_04F2&PID_B604&MI_00` under the Local
  Service account. It is not a universal Windows darkness switch and was not
  imported or adapted to this camera.
- [An exact 3277:0060 Linux report](https://forums.linuxmint.com/viewtopic.php?p=2531683)
  resolved a general webcam-opening problem through app removal/reboot/device
  selection. It does not demonstrate a fix for dark-room privacy blanking.
- [Dell camera reverse engineering](https://github.com/SeeleVolleri/research_0jcxg0)
  describes register access on a different camera. It is not a validated
  darkness fix for this module; no register-address or firmware writes were
  attempted.

## Covered-RGB illumination-mode comparison

Following the read-only research, the user approved a temporary mode comparison
and confirmed readiness after being instructed to cover only the RGB lens while
leaving the IR lens and emitter clear. The room was bright. Lens placement was
user-controlled, not independently inspected. This remains a covered-lens test,
not a genuine uncovered dark-room reproduction.

A temporary native probe opened only the exact physical IR sensor using Media
Foundation, selected native 640x400 Gray8 with converters disabled, and measured
three approximately eight-second phases on one stream. It used an asynchronous
Source Reader with a five-second frame wait and a 45-second outer watchdog.
Only frame statistics were saved, not images. Fresh FaceAuth GETs and privacy
GETs were taken approximately once per second while streaming.

| Phase | Verified mode | Frames | Uniform gray (144) | Nonuniform | Privacy readings |
| --- | --- | --- | --- | --- | --- |
| Baseline | `0x1` general-purpose | 119 | 119 | 0 | All 1 |
| Alternating IR | `0x2` alternating illumination | 121 | 121 | 0 | All 1 |
| Restored baseline | `0x1` general-purpose | 122 | 122 | 0 | All 1 |

The mode SET and readback succeeded. Alternating mode remained reported as
`0x2` throughout its phase, but no image detail returned. This rules out a simple
live mode switch as a remedy for **this covered-RGB condition**. It does not
prove that every startup sequence or an uncovered dark-room comparison behaves
identically. No USB trace or visual frame inspection was performed in this test.

The probe restored the initial `0x1` mode, flushed and shut down successfully,
and exited with code 0. The watchdog did not fire. A separate fresh-source GET
after shutdown independently confirmed `0x1`, then closed normally. Both
physical cameras and the DFU device reported OK, with zero diagnostic processes
remaining. No drivers, firmware, registry settings, or production capture code
were changed. The subsequent uncovered recovery check is recorded below.

The ignored local log is under
`build/mode-covered-e6f26259086142c798dfef853866d4fc/probe.log`.
The machine-specific comparison source/build script remains experimental under
`build/usb-trace-tools/`; it is not a shipped capture option.

### Uncovered recovery control

The user then confirmed both lenses were uncovered. An approximately eight-second
baseline-only run used the same Media Foundation source/reader and native Gray8
format, without selecting alternating mode. It delivered 117 frames: all 117
nonuniform, zero uniform frames, and zero gray-144 frames. Every sampled privacy
GET returned 0; FaceAuth remained `0x1` throughout. Periodic frame standard
deviations were approximately 29.4-32.3, versus exactly zero when covered.

This confirms that uniform blanking cleared after uncovering in the bright-room
control. Frame statistics do not independently establish visual face-image quality.
It does not demonstrate an uncovered dark-room workaround.

Flush, original-mode restoration and source shutdown succeeded; process exit was
0 and no watchdog termination was needed. A fresh-source, non-streaming GET again
confirmed `0x1`. Both cameras and the DFU device reported OK, and no diagnostic
processes remained. No frames were saved. The ignored local statistics log is
`build/mode-uncovered-33892d6b6a11458e8e16f4797bda1dc0/probe.log`.

## Remaining validation

With user approval and a genuinely dark room, both lenses uncovered:

1. Open only the IR camera, record its initial mode and capabilities.
2. Measure live-frame variation, delivery, and privacy state in the baseline.
   Query mode again after streaming begins to detect automatic mode changes.
3. Select only the documented, advertised alternating-illumination mode, and
   verify the current mode again. If already active, it is not a new workaround.
4. Compare under unchanged light, then restore the initial mode and release all
   capture resources. Enforce an outer timeout, with explicit recovery/verification
   if forced termination prevents normal restoration.
5. A useful result requires sustained, detailed live frames, not merely a
   successful setting call or intermittent nonuniform noise. Then repeat through
   UmbraCam and a browser before calling it a fix.

The initial research phase was GET-only, with no video streamed or saved.
The later approved comparison above temporarily changed the documented mode
and streamed statistics-only samples, then restored and verified the initial
mode. It did not establish a fix to ship in UmbraCam.

If the supported mode does not prevent blanking, the remaining software route
needs an OEM-documented control or corrected firmware. The current evidence
does not justify promising a registry-only fix. A support request should ask
ASUS to investigate false RGB occlusion causing IR image replacement on this
exact module, and provide a supported remedy that preserves normal privacy
controls. Raw traces contain camera images and must not be attached publicly.

## Primary control references

- [Microsoft extended camera controller access](https://learn.microsoft.com/en-us/windows/win32/api/mfidl/nn-mfidl-imfextendedcameracontroller)
- [FaceAuth modes and capability flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/ksproperty-cameracontrol-extended-faceauth-mode)
- [Microsoft UVC FaceAuth and IR-torch selectors](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/uvc-extensions-1-5)
- [Firmware-based shutter detection and false positives in darkness](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/camera-privacy-controls#isp-firmware-based-state-detection)
- [Privacy reporting is not a writable shutter override](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/privacy-shutter-notification)

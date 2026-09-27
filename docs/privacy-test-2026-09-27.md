# Dark-room camera privacy investigation — 2026-09-27

## Outcome

The IR camera stack reports privacy/occlusion active as its image becomes a
uniform gray value of 144. This is visible both through Windows MediaCapture
and through direct Kernel Streaming, below MediaCapture, Frame Server and
user-mode Device MFT processing. Changing UmbraCam's capture API alone did not
recover useful dark-room frames.

This narrows the cause to the kernel-driver/device side of the capture path.
It does **not** distinguish firmware/ISP image replacement from processing in
the installed kernel driver stack. No USB packet trace or firmware inspection
was performed. A reported hardware-occlusion bit is not proof of a physical
shutter being closed.

## Conditions and safety

- Development machine: ASUS GA403WR; built-in USB RGB/IR camera, VID 3277/PID 0060.
- User confirmed a dark room, flashlight off, and both lenses physically uncovered.
- Both camera interfaces were enabled at preflight and after testing. The user
  had previously manually disabled RGB; these diagnostics did not toggle it.
- No driver installations, registry writes, device-disable operations, exposure
  changes or privacy-control writes. Kernel streaming uses normal pin
  acquire/pause/run/stop operations only.
- No images/video saved or uploaded. Local ignored logs contain statistics and
  device identifiers; only aggregate findings are committed.
- The physical camera can illuminate its visible activity LED during IR-only
  capture. The LED is not an RGB-only activity indicator.

## Measurements

| Test | Result |
| --- | --- |
| Windows MediaCapture, standalone IR group, 30 s | 446 frames: 434 uniform, 12 nonuniform. Native L8. Shutter changes from open at start to `occluded=1, hardware=1, lid=0` at about 1 s; gray frames occur alongside it. |
| DirectShow/KSProxy | Sensor interface discovered, but camera filter binding fails with `0x80070002`. No samples captured through this route; not a successful image comparison. |
| Idle direct kernel properties | Camera interface opens successfully; privacy GET succeeds with value 0; two pin factories. Idle privacy readings are not authoritative for streaming. |
| Direct Kernel Streaming, 15 s | Native L8-IR, 640×400, stride 640. 103 frames: 100 uniform, 3 nonuniform. Privacy becomes 1 at 0.89 s; first observed flat-gray frame at 1.02 s. Privacy remains 1 through the remaining sampled interval. |
| Additional kernel run, 60 s | 420 frames: 335 uniform, 85 nonuniform. Privacy becomes 1 around 0.89 s and returns to 0 around 48.62 s; nonuniform frames are logged again at 49.62 s. |
| Cleanup | Each completed capture stops and releases its reader/pin; no publisher or diagnostic processes remain. RGB and IR both report healthy afterward. |

The 60-second run was intended as a flashlight transition test, but the user
reported missing the on-prompt. Its late recovery has **uncontrolled lighting**
and must not be presented as a completed dark/light/dark experiment. No off-cycle
was completed in that run.

The kernel probe submits one frame read at a time and performs periodic property
queries. Its delivered frame rate is not a camera-performance benchmark. A
nonuniform frame can be noise or an ambient exposure; no visual quality claim
is made from statistics alone.

## Earlier comparison, same investigation

The temporary RGB-disable test succeeded after Firefox released the normal
camera. Windows confirmed RGB Code 22 and IR healthy; 651/655 IR frames were
uniform over 45 seconds. A subsequent test with RGB manually disabled by the
user yielded 649/654 uniform frames. Both reproduced the blanking. These tests
do not establish whether RGB disabling still helps Windows Hello authentication.

## Software validation

- MSVC C++20 `/W4` diagnostic build succeeds.
- Native statistics tests pass for constant pixels, varied pixels, row padding,
  and seven invalid/overflowing buffer cases.
- PowerShell runner parses successfully; four invalid parameter cases and two
  native CLI argument cases are rejected before camera access.
- Successful hardware runs return exit code 0; failed capture paths return
  nonzero with the failing stage and HRESULT logged.
- Production publisher and virtual-camera source are unchanged; these are
  opt-in, non-installing diagnostics, not a shipped workaround.

See [usage and API references](camera-privacy-diagnostics.md). Important primary
references for interpreting these findings:

- [Microsoft: privacy property 0/1 meanings](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/ksproperty-cameracontrol-privacy)
- [Microsoft: false shutter detection in darkness and RGB/IR LED behavior](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/camera-privacy-controls)
- [Microsoft: occlusion reporting limitations](https://learn.microsoft.com/en-us/uwp/api/windows.media.devices.cameraocclusioninfo)

## Remaining uncertainty

A USB-level comparison or vendor explanation would be needed to distinguish
camera firmware/ISP blanking from kernel-driver replacement. An OEM driver or
firmware change could be investigated separately, but no such change is tested
or recommended as a confirmed fix here. The lower-level path is not a working
dark-room workaround on this machine.

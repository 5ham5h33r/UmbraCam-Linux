# FaceAuth startup-order diagnostic

Machine-specific research for USB VID 3277 / PID 0060 / IR interface MI_02.
This is not an UmbraCam production option or a verified privacy-blanking fix.
It never opens the RGB interface, installs a driver, or writes the registry.
It captures native 640x400 Gray8 and prints statistics, not images.

Build with `scripts\build-faceauth-probe.cmd` using the same Visual Studio 2022
Build Tools and Windows SDK 10.0.26100.0 as the privacy probe. The executable
is `build\umbracam-faceauth-probe.exe`.

## Explicit modes

- `--query`: open the IR source, read FaceAuth flags/capabilities, then close.
- `--baseline`: stream the initial mode for eight seconds.
- `--compare`: eight seconds each of initial mode, alternating illumination,
  and restored initial mode on the same stream.
- `--prestart`: set alternating illumination before creating the Source Reader
  or selecting its format, then capture for 60 seconds. Query mode after reader
  creation, format selection, and before the first read; continue querying mode
  and privacy approximately once per second.
- `--restore-general`: explicit recovery to general-purpose mode (`0x1`), only
  when that was the known original state after an interrupted experiment.

All streaming modes arm restoration before any property SET and attempt to
restore the original mode on normal/error exit, flush, and shut down the source.
Invalid arguments return 2 before opening hardware. Each asynchronous frame wait
is bounded to five seconds. Driver/API calls and cleanup can still hang: run
under an external process watchdog (85 seconds for `--prestart`). If termination
is necessary, re-query and explicitly restore the recorded original mode; do not
claim that killing the process restored it. Do not indiscriminately kill other
camera apps. Close them normally before testing.

Keep the lighting/coverage fixed during each run and record it accurately:
covered RGB in a bright room is not the same experiment as both lenses uncovered
in darkness. Nonuniform statistics alone do not prove recognizable face video.
Check restoration with a separate `--query` after capture exits. Raw diagnostic
logs belong under ignored `build/`, with aggregate findings in `docs/`.

## Motivation

The user reports successful Gaze face authentication in complete darkness on
Linux. In the examined upstream source, Gaze enables its emitter before opening
IR capture. Its generic USB fallback uses Microsoft FaceAuth alternating mode.
The user's exact installed version/profile has not been collected. Authentication
success establishes usable IR images, not sustained video-call streaming.

- [Gaze startup order, pinned revision](https://github.com/GunduLabs/gaze/blob/9878608830e44b9480a50575ff34d0a69bbc13ab/gaze/src/daemon.rs#L4368-L4396)
- [Gaze IR controls, same revision](https://github.com/GunduLabs/gaze/blob/9878608830e44b9480a50575ff34d0a69bbc13ab/gaze-core/src/ir/led.rs)
- [Microsoft FaceAuth modes](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/ksproperty-cameracontrol-extended-faceauth-mode)

This probe extends our own existing diagnostic and uses Windows SDK interfaces;
it does not incorporate Gaze source code. Opening a Media Foundation source is
still a Windows-specific operation: setting the mode before the Source Reader
does not reproduce every Linux/UVC initialization request.

## Windows RGB/IR startup comparison

`scripts\build-startup-probe.cmd` builds two additional, machine-specific tools:

- `umbracam-camera-profiles.exe --inventory`: enumerate physical 3277:0060
  camera/source-group profiles without initializing MediaCapture or streaming.
  An unavailable per-profile concurrency query is logged separately from an
  empty concurrent-profile list, and does not truncate subsequent profiles.
- `umbracam-startup-probe.exe <mode> <seconds 1..60>`: select only the physical
  RGB MI_00 / IR MI_02 groups (or their exact combined group in the explicit
  group mode); compare bounded capture phases. No frame files,
  property SETs, registry writes, device enable/disable, or driver installation.
  Selecting a profile/format can change session configuration through Windows;
  this is not a claim that camera initialization sends no USB control requests.

Prefer the supervised runner, with the actual user-confirmed test condition:

```powershell
.\scripts\test-camera-startup.ps1 -Mode rgb-first -Condition bright-rgb-covered -Seconds 20
```

Modes:

| Mode | Sequence |
| --- | --- |
| `ir-only` | IR alone for the requested duration |
| `rgb-first` | RGB alone 3 s, then both for the requested duration |
| `ir-first` | IR alone 8 s, then both for the requested duration |
| `rgb-release` | RGB alone 3 s, both 8 s, close RGB, IR for the requested duration |
| `faceauth-ir` | Advertised FaceAuth profile, IR alone for the requested duration |
| `faceauth-pair` | Advertised FaceAuth profiles, RGB alone 3 s, then both |
| `faceauth-group` | One exact RGB+IR group with its FaceAuth profile; start both readers |

Default-profile formats are explicitly native IR L8 640x400/15 and RGB
NV12/YUY2 640x480/15. FaceAuth modes select the exact enumerated profile and its
single record description, logging the resulting format. No fabricated profile
or unadvertised format is forced. Per-device profiles do not themselves prove
concurrency is supported; a rejected pair is an inconclusive opening failure,
not evidence about privacy blanking while streaming.
The group mode closes both readers before closing their shared MediaCapture
owner. Its occlusion reports are capture-group state, not independently queried
IR/RGB device controls. No unrelated group members are permitted.

IR statistics inspect CPU Gray8 pixels without conversion. RGB is counted only
to verify ongoing delivery; its pixel content is not measured or saved. Both
streams are polled, timestamps deduplicated, and absence of new frames for five
seconds is an error. CameraOcclusionInfo support and in-stream state are logged.
No nonuniform-frame count alone proves recognizable or acceptable video.

Close other camera apps normally. The runner checks exact device health, records
the condition and timestamps, and provides a process watchdog (duration +35 s).
It terminates only the process it started if necessary. Each stream is stopped
and closed on ordinary/error exit; failures are reported. A watchdog kill is NOT
proof of normal cleanup or restored properties. Independently check the activity
indicator and query FaceAuth mode before/after a batch using the earlier probe;
investigate any change before further capture. Do not automatically reset an
unknown initial mode. Logs remain ignored under `build/`.

Offline argument/runner checks: `tests\startup-probe-tests.ps1`. Invalid binary
arguments return 2 before WinRT initialization or any camera discovery.

- [Profile initialization semantics](https://learn.microsoft.com/en-us/uwp/api/windows.media.capture.mediacaptureinitializationsettings.videoprofile)
- [Supported-profile enumeration](https://learn.microsoft.com/en-us/uwp/api/windows.media.capture.mediacapture.findallvideoprofiles)

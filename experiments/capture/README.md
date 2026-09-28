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

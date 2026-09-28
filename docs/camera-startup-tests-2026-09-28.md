# Windows RGB/IR startup and profile comparison

## Result

No sustained workaround was found in seven bounded comparisons. Both physical
streams can run concurrently, but that does not prevent IR privacy blanking.
Explicit FaceAuth profiles, including the combined Realtek source group, also
failed to provide sustained nonuniform frames under the tested condition.

The user confirmed **a bright room with only RGB covered, leaving IR and the
illuminator clear**. This is not an uncovered dark-room test. Coverage and light
were user-controlled, not independently measured. The earlier reported Linux
success remains relevant; this experiment does not prove hardware failure or
that every Windows USB initialization sequence is equivalent.

No driver bindings, registry settings, device enabled states, firmware,
production capture code, or explicit camera-control properties were changed.
Opening streams and selecting formats/profiles still configures the capture
session through Windows. No images, video, or USB payloads were saved.

## Profile inventory

`list-camera-profiles.cpp` queried the exact 3277:0060 physical interfaces and
their combined `Rts-DMFT-Group`, without starting capture:

| Device/group | Profiles returned | Concurrent profiles returned |
| --- | ---: | ---: |
| RGB MI_00 | 4 | 0 |
| IR MI_02 | 2 | 0 |
| Combined RGB+IR | 2 | 0 |

All three enumerate the Legacy profile and FaceAuth profile
`{81361B22-700B-4546-A2D4-C52E907BFC27},0`, identified against the installed
Windows SDK's `KSCAMERAPROFILE_FaceAuth_Mode` definition. FaceAuth advertises:

- Physical IR: one record description, L8 640x400 at 15 fps.
- Physical RGB and combined group: one record description, YUY2 1920x1080 at
  30 fps. The combined group's initialized frame sources also exposed IR.

`GetConcurrency()` on FaceAuth returned `0x80070490` (element not found), unlike
the successful top-level query returning zero concurrent profiles. The tool
preserves this distinction and continues enumeration. The initial inventory
implementation stopped at that optional query failure; the corrected run
completed successfully. Absence of advertised concurrency did not prevent
actual paired streaming in the tests below; it is not proof of a guaranteed
supported combination either.

## Capture protocol

The diagnostic uses exclusive WinRT MediaCapture sessions, CPU frame buffers,
native formats, timestamp deduplication and five-second no-fresh-frame guards.
The runner records conditions/device health and enforces an independent process
watchdog of requested duration +35 seconds. It never kills unrelated processes.

Default-profile tests explicitly select IR L8 640x400/15 and RGB NV12
640x480/15. FaceAuth tests pass the enumerated profile and its exact record
description into initialization; logged resulting formats match those above.
This verifies successful API initialization with the requested profile, not
the camera's internal interpretation of every driver hint. No USB control
trace or independent active-profile GET was taken in this batch.

RGB frames are counted to verify delivery, without pixel analysis. IR statistics
inspect Gray8 pixels without conversion. A flat frame means every analyzed
pixel has the same value; every flat IR frame in these tests had value 144.
Nonuniform frames do not, by themselves, prove recognizable face video.

## Covered-RGB results

Counts are for the final **20-second phase**, excluding warmup phases:

| Test | IR frames acquired | Flat gray 144 | Nonuniform | RGB frames acquired |
| --- | ---: | ---: | ---: | ---: |
| IR alone, default | 287 | 275 | 12 | 0 |
| RGB first, then both | 298 | 298 | 0 | 295 |
| IR first, then both | 299 | 284 | 15 | 288 |
| Start both, then release RGB | 301 | 292 | 9 | 0 |
| IR alone, FaceAuth profile | 292 | 282 | 10 | 0 |
| RGB first, both FaceAuth profiles | 294 | 294 | 0 | 295 |
| One combined FaceAuth group, both readers | 298 | 292 | 6 | 298 |

Additional phases:

- RGB-first: 3-second RGB warmup acquired 33 frames.
- IR-first: 8-second IR warmup acquired 109 frames, 99 flat and 10 nonuniform.
- RGB release: RGB warmup acquired 34 frames; the 8-second paired phase
  acquired 117 IR frames (all flat) and 116 RGB frames before RGB was closed.
- FaceAuth pair: RGB warmup acquired 37 frames.

The reverse-order test briefly recovered nonuniform IR data after RGB started:
the report around 1 second into the paired phase showed IR standard deviation
64.515, but it was flat again around 2 seconds and stayed flat. Releasing RGB
also yielded nine transient nonuniform frames, then gray again. These are
interesting state-transition observations, not a video-call workaround. They do
not establish whether the camera firmware, stream initialization, or another
state change accounts for the brief recovery.

CameraOcclusionInfo reports followed the flat/occluded state. With independent
physical sessions, hardware occlusion support was reported for each camera.
In the combined-group test, occlusion is a group-level report, not two
independent physical-property reads. Acquired-frame counts are not sensor FPS
measurements: the reader polls latest frames and can skip frames, particularly
during initialization or other API calls.

## Cleanup and validation

All seven runs exited 0, with successful reader stop and capture close logs.
The combined capture owner was closed only after both readers were stopped.
No watchdog termination was needed. Both physical cameras, the USB composite
device, and the DFU device reported OK afterward. No UmbraCam/diagnostic
processes remained.

A separate fresh-source FaceAuth query before the batch reported flags `0x1`,
capabilities `0x3`; another after all seven runs reported the same values and
shut down successfully. No corrective property SET was needed. Device health
and process exit do not alone prove the physical indicator is off; user
confirmation and an uncovered recovery comparison were requested separately.

Build succeeded. Offline checks passed: runner syntax, four runner parameter
guards, 17 invalid executable invocations before camera access, the earlier five
FaceAuth argument guards, and existing privacy-runner/Gray8 statistics tests.

## Local evidence (ignored, not published)

All paths below are relative to `build/`:

- `profiles-9d771bd5d025438e83d4325b853fe945/` (complete inventory and initial mode)
- `startup-ir-only-4b66355afa69471898c1212404925ab4/`
- `startup-rgb-first-3f46102bbd2f457da8074ba6c3e5f754/`
- `startup-ir-first-286d68adf1ab4931b6196c149b025755/`
- `startup-rgb-release-7f4aebbbcc7f457d83dd703fe9e5cca4/`
- `startup-faceauth-ir-b1d78cf3a00f4907924234e4aba11d46/`
- `startup-faceauth-pair-d260ed6ee0044f428cce72453f04ec9a/`
- `startup-faceauth-group-3d1fdd01cb024fd09cb05846c5cf9ec2/`
- `startup-postcheck-40239d6112a446358f45387008acfc20/` (final mode query)

## Next Windows-only candidates

The user does not want a Linux boot/USB tracing task. That is not a prerequisite
for continued investigation. These results reduce confidence in a simple
startup-order or profile-selection fix. Remaining candidates are a supported
basic-camera/plugin-bypass comparison where applicable, and targeted mapping
of manufacturer extension controls using documented Windows APIs. Do not
silently broaden this into global policy changes, arbitrary register writes,
or driver rebinding. A WinUSB capture backend remains a separate opt-in,
invasive experiment with rollback and RGB/Hello impact considerations.

- [Profile initialization semantics](https://learn.microsoft.com/en-us/uwp/api/windows.media.capture.mediacaptureinitializationsettings.videoprofile)
- [Supported profiles](https://learn.microsoft.com/en-us/uwp/api/windows.media.capture.mediacapture.findallvideoprofiles)
- [Camera profile sensor-group concurrency](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/camera-profile-v2-sensor-group-generation)
- [Windows basic-camera policy](https://learn.microsoft.com/en-us/windows/client-management/mdm/policy-csp-camera#configurecameraoptions)

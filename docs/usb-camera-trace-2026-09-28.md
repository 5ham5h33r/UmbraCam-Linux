# USB camera trace: covered and bright-room controls

## Result

The covered-camera control contains complete 640x400 IR frames whose pixel
bytes are already uniformly 144 in the incoming USB transfer payloads. The
camera also returns privacy state 1 in USB control responses. The uncovered,
bright-room control contains nonuniform frames and privacy state 0.

This strongly supports camera-side privacy blanking for the **covered** case,
before UmbraCam, browser processing, or ordinary Media Foundation processing.
It is not yet a USB-level reproduction of the **uncovered dark-room bug**.
The user reported a bright room and covered the camera to simulate darkness;
physical coverage can legitimately cause occlusion. The exact lenses covered
were not separately confirmed. Both lenses were confirmed uncovered for the
second capture.

USBPcap observes USB request blocks inside Windows, not electrical packets
using an independent hardware analyzer. These tests are not firmware inspection
and do not prove that every Windows component is uninvolved.

## Setup

- ASUS GA403WR built-in RGB/IR USB camera; VID 3277/PID 0060.
- Official signed USBPcap 1.5.4.0 installed with user approval; driver active
  following the user's restart. No further driver installations in these tests.
- Explicit address filtering: root hub 4, device address 1 in this boot/session.
  Both saved traces contain only that bus/address. These numbers must be
  re-enumerated before future tests; they are not portable camera identifiers.
- Kernel IR probe streamed for 15 seconds per condition. USB recording began
  before initialization and continued through camera shutdown.
- The configuration descriptor associates the IR streaming interface 3 with
  endpoint 0x82, native L8-IR (8-bit), 640x400, 256,000 bytes per image.
- Snap length 1 MiB; USBPcap buffer 16 MiB. Only the IR stream was explicitly
  opened. Normal camera initialization also queried some RGB control properties;
  no RGB image endpoint traffic was found in the covered trace inventory.
- No custom privacy SET, firmware commands, device toggles, or registry tweaks.

## Measurements

| Observation | Covered, bright room | Uncovered, bright room |
| --- | --- | --- |
| Complete reconstructed USB IR frames | 226 | 228 |
| Uniform gray frames, value 144 | 207 | 0 |
| Other uniform frames | 9 at value 255 during startup | 0 |
| Nonuniform frames | 10 | 228 |
| IR USB privacy responses | 0 initially, then 1 | 0 throughout |
| Kernel probe delivered frames | 108 | 110 |
| Kernel probe uniform frames | 107, value 144 | 0 |
| Kernel streaming privacy | Becomes 1 | Remains 0 |

USB frame counts and probe frame counts are not expected to match. The kernel
probe submits one read at a time and is not a throughput benchmark. Nonuniform
does not establish image quality; no visual inspection was performed.

The read-only analyzer stripped each UVC payload header and reconstructed images
using frame-ID and end-of-frame flags. It counted only complete 256,000-byte
frames without detected UVC/isochronous errors. Five synthetic checks passed:
constant gray, varied pixels, short-frame exclusion, truncated packet rejection,
and truncated record-header rejection.

Privacy was read from matched USB GET_CUR control request/response pairs:
selector 0x11, IR control interface 2, camera terminal 9. Microsoft documents
the mapping from the Windows privacy property to UVC CT_PRIVACY_CONTROL.

## Capture limitations and cleanup

Both IR probe runs stopped and released their streaming pins normally. The
USB recorder did not respond to the attempted console quit signal in either
run; the supervisor terminated only its own recorder after a five-second
shutdown timeout. The shutdown adjustment between runs did **not** resolve it.

Neither file contains a truncated PCAP record. Both contain one incomplete
final video frame, excluded from counts. Forced recorder termination can still
lose buffered tail data, so these captures must not be described as lossless
or gracefully completed. Earlier complete frames support the observations above.
The first supervisor did not retain the probe exit code; its probe log reports
normal completion. The second probe exit code was 0.

Afterward no USBPcapCMD or IR probe processes remained. Both physical cameras
reported OK. The USBPcap filter remains installed; no recording is running.

Raw captures contain camera-image data and remain local under ignored `build/`
directories (approximately 62.6 MB and 63.1 MB). They are not committed or
uploaded. The temporary capture supervisor/analyzers also remain under `build/`;
they are experimental, machine-specific diagnostics, not supported repo tools.

## Next controlled test

Repeat the camera-only trace in a genuinely dark room with **both lenses
uncovered**, and compare the incoming frame bytes and USB privacy responses.
Fix graceful recorder shutdown before further routine captures. Do not treat
covering the IR lens as a substitute for an unobstructed night-vision test.

If real darkness also produces gray frames in incoming USB payloads, changing
UmbraCam's normal capture API cannot recover pixels already replaced upstream.
An OEM firmware explanation/fix or a separately validated device-specific
approach would then be the relevant direction, not a proven Windows registry fix.

## References

- [USBPcap capture format](https://desowin.org/usbpcap/captureformat.html)
- [USBPcap capture limitations](https://desowin.org/usbpcap/capture_limitations.html)
- [Microsoft privacy shutter notification and UVC mapping](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/privacy-shutter-notification)
- [Microsoft firmware-based shutter detection and darkness caveat](https://learn.microsoft.com/en-us/windows-hardware/drivers/stream/camera-privacy-controls#isp-firmware-based-state-detection)
- [Earlier uncovered dark-room kernel tests](privacy-test-2026-09-27.md)

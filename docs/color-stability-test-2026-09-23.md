# Motion-tracked IR color experiment — 2026-09-23

## Outcome

A new clip shows substantially steadier colors using a fixed initial color
reference plus motion-compensated color memory. This is a useful offline proof
of concept, **not** proof of accurate colors or a live-call-ready feature.
The installed UmbraCam publisher and physical camera drivers remain unchanged.

The helper discarded two seconds of camera startup and then recorded eight
seconds of IR, no audio: 60 unique illuminated frames, 640x400, measured
7.4946 fps. The subject turns their head, providing a harder motion test than the
previous clip. Capture ended successfully; the camera was released.

## Method

- DDColor-L ModelScope generates colors with FP16 autocast on the RTX 5070 Ti
  Laptop GPU. No external reference photo or true RGB ground truth was used.
- RAFT-small C_T_V2 estimates current/previous and current/initial-reference
  motion in both directions, with FP32 CUDA inference.
- Forward/backward consistency, in-bounds coordinates and brightness agreement
  gate reference reuse. Matched reference colors stay fixed to their tracked
  image content. Other reliable pixels blend 98% warped history / 2% new chroma;
  newly revealed/unreliable pixels get fresh model color.
- A whole-scene mismatch resets the reference. There were no resets in this
  clip after the initial frame.
- Only Lab chroma is carried between frames. Current IR luminance/detail is
  used every frame; no freezing or temporal averaging of RGB textures.
- Baseline and stabilized outputs use the same color predictions and chroma
  resolution, with no saturation-reduction trick.

The processing is causal: the first frame is the initial reference, and no
future frame is used. It can still lock in an incorrect initial color.

## Same-clip comparisons

| Setting | Motion-aligned chroma variation reduction | Moving-pixel subset | Mean processing | p95 processing |
| --- | ---: | ---: | ---: | ---: |
| RAFT 320x200, 12 updates | 90.16% | 87.00% | 385.0 ms | 533.7 ms |
| RAFT 256x160, 6 updates | 87.87% | 85.21% | 145.3 ms | 165.0 ms |

Recommended **offline preview**: the 256x160/six-update variant. In that run:

- Mean DDColor inference/pre/postprocessing: 93.7 ms per frame.
- Combined color, motion and two-output rendering: 145.3 ms/frame, about 6.9 fps;
  max 184.0 ms. This is slower than the approximately 133.4 ms source interval.
- Peak PyTorch allocated memory: 1771.8 MiB (not total GPU/process memory).
- Mean reference-match coverage: 90.1%.
- Mean absolute chroma: baseline 8.16, stabilized 9.16; improvement is not from
  making the output grayscale.
- Motion-aligned mean absolute Lab chroma difference, tracked pixels:
  baseline 5.415, stabilized 0.657. Moving subset: 5.946 versus 0.880.

The motion metric uses **the same algorithm's own flow and confidence mask** for
both sequences, so it favors motion-warped output and is not an independent
perceptual quality benchmark. It cannot establish absence of ghosting or color
accuracy. Timing excludes disk, encoding, evaluation metrics, capture and Meet;
includes current preprocessing/transfers, both networks, chroma warping and both
rendered outputs. Five model warmups precede processing, and the bootstrap frame
is excluded from summary timing. Both runs were sequential, not a controlled
power/thermal benchmark; some speed difference may reflect operating conditions.

## Visual findings and remaining limits

Representative frames at roughly 0, 2, 4 and 8 seconds show the independent model
switching the background between warm orange and cool blue. Reference memory
largely holds the warm background and keeps face/hair colors more consistent
through head turns, while pose and detail update normally.

Small colored edge/occlusion artifacts remain, especially near the image's right
edge. Bright IR face/eye highlights remain washed out. The reference has a warm
color cast which is preserved, not corrected. Newly seen objects can still get
incorrect or changing colors. Eight seconds does not validate long calls, large
scene changes, hands crossing the face, different people, lighting changes,
thermal throttling or other GPU workloads. Synthetic tests cover match rejection
and color-memory fallback, not all such real-world cases or the full scene-reset path.

Do not enable this in Meet yet: first review the preview, then improve quality
and latency, and test on a longer diverse clip before live integration.

## Validation and artifacts

- Six CUDA regression tests passed: identity warp, translation direction and
  border validity, photometric mismatch, forward/backward inconsistency,
  100-step anchored-color retention and disocclusion refresh.
- Python syntax, Git whitespace checks, capture helper build and real capture passed.
- H.264 output fully decoded successfully: 60 frames, 640x400 stabilized view,
  8.006 seconds, no audio. Three-panel comparison: 1920x440.
- Runtime PyTorch 2.7.1+cu128; torchvision 0.22.1+cu128. Both models assert CUDA,
  and checkpoints load with `weights_only=True`, `strict=True`. No CPU fallback.
- No uploads, camera-setting changes, driver changes or live-app installation.

Ignored local artifacts: `build/color-stability-20260923/`:

- `raw/`: new input frames and source timestamps.
- `anchored-v1/`: more expensive tracking run.
- `anchored-fast/`: comparison.mp4, stable.mp4, preview/contact sheet,
  per-frame PNGs and metrics.json.

Source and result notes are committed; all recorded and generated footage,
models, caches and environments remain Git-ignored and were not pushed.

References: [PyTorch RAFT-small](https://docs.pytorch.org/vision/stable/models/generated/torchvision.models.optical_flow.raft_small.html),
[optical-flow input and direction conventions](https://docs.pytorch.org/vision/stable/auto_examples/others/plot_optical_flow.html),
[DDColor source](https://github.com/piddnad/DDColor).

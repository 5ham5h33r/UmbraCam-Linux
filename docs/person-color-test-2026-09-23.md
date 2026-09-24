# Person-first IR color experiment — 2026-09-23

## Outcome

Person-first segmentation and cropping worked on all 60 frames of the existing
eight-second clip. It removes actual background context from color inference,
but **did not solve the unnatural skin/hair colors**. The person-first output
has a pink/purple cast, whereas the earlier full-frame output looks warmer.
Without RGB ground truth, neither can be called accurate. This remains an offline
prototype; the installed camera, publisher and drivers were not changed.

No new capture or camera activation was needed. The input is the earlier
640x400 IR clip at 7.4946 unique frames/sec, including head turns, with no audio.
All processing was local on the RTX 5070 Ti Laptop GPU; no footage was uploaded.

## Pipeline and comparison

1. DeepLabV3-MobileNetV3 segments the largest connected person region on CUDA.
2. A causal, smoothed square crop contains the silhouette. Its background is
   neutral gray before DDColor-L predicts colors; it is not merely masked later.
3. RAFT-small color memory reuses chroma only where current and source masks both
   indicate the person. Current IR luminance is used every frame.
4. The full-resolution silhouette composites color onto the original grayscale
   scene. A two-pixel inward trim reduces uncertain boundary coloring, at the cost
   of a narrow grayscale rim. It does not eliminate all colored hair-edge artifacts.

The final fair comparison is original IR / full-frame color memory masked
afterwards / person-first color memory. Both color variants use the same person
matte and grayscale background. This separates person-first inference from the
cosmetic benefit of removing a colored background. Crop magnification also differs,
so background context is not the only changed variable.

Representative frames at 0, 2, 4 and 8 seconds show remaining purple/pink casts
and blown-out IR highlights. Isolating the person alone is not a sufficient fix.
The visible-light-trained colorizer and segmenter still face an IR domain mismatch;
the initial color reference can lock in an incorrect guess.

## Final run: focused-v2

| Measurement | Result |
| --- | ---: |
| Frames with detected person | 60 / 60 |
| Mean processing time | 280.2 ms/frame (about 3.6 fps) |
| p95 processing time | 318.8 ms/frame |
| Peak PyTorch allocated memory | 1816.1 MiB |
| Reference resets after bootstrap | 0 |
| Person-region motion-aligned chroma change | 3.319 independent / 0.386 stabilized |
| Moving-person subset chroma change | 3.566 independent / 0.467 stabilized |

The 88.37% / 86.90% reductions compare independent and stabilized **person-crop**
predictions, using the algorithm's own flow/confidence. They are not independent
quality scores and do not establish accurate colors or absence of ghosting.
Do not compare them directly with previous full-frame percentages: the evaluated
regions differ. The reference-coverage field divides by all image pixels, not
person pixels; low values do not directly mean tracking failure.

DDColor uses FP16 autocast, segmentation and RAFT use FP32 CUDA. CPU handles mask
cleanup/compositing and I/O; there is no neural-network CPU fallback. Runtime:
PyTorch 2.7.1+cu128, torchvision 0.22.1+cu128. Strict, weights-only checkpoint loading.

Timing includes segmentation, crop/color inference, flow, transfers, blending and
two rendered outputs, but excludes evaluation metrics, disk, encoding, capture
and Meet. Five warmups and the bootstrap frame are excluded from summary timing.
This is a short sequential experiment, not a controlled thermal or live-call test.
It is slower than the approximately 133 ms interval between source frames.

## Validation and artifacts

- Seven geometry/mask tests passed: background isolation for a fixed silhouette,
  detection-loss reset, padded crop coordinates, bounding-box containment/no shrink,
  small-component rejection, inward feathering and conservative boundary trimming.
- Six existing CUDA color-memory regression tests passed.
- All 60 lossless frames retain byte-identical original pixels outside the matte.
  This property is asserted at runtime and checked again by the comparison script;
  lossy H.264 can slightly alter pixels around edges.
- Final stabilized and fair-comparison H.264 videos fully decoded without errors.
  Stabilized video: 60 frames, 640x400, 8.005791 seconds, no audio.
- No camera setting writes, driver installation, live publisher changes or uploads.

Ignored artifacts under `build/person-color-20260923/`:

- `focused-v1/`: initial silhouette experiment.
- `focused-v2/`: final inward-trimmed output, metrics, masks and model-input crops.
- `fair-comparison-v2/comparison.mp4`: same-background three-panel comparison.
- `fair-comparison-v2/contact-sheet.png`: representative temporal frames.

Code and notes are committed; footage, checkpoints and environments stay ignored.
The current test does not validate multiple people, hands crossing the face,
detection loss in real footage, longer sessions or performance alongside Meet.

Model reference: [torchvision DeepLabV3-MobileNetV3](https://docs.pytorch.org/vision/stable/models/generated/torchvision.models.segmentation.deeplabv3_mobilenet_v3_large.html).

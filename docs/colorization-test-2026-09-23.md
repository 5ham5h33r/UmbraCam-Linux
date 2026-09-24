# Local IR colorization feasibility test — 2026-09-23

## Outcome

Local inference can keep up with the current illuminated-only IR feed on the
RTX 5070 Ti Laptop GPU in this short offline test. **Off-the-shelf color quality
is not ready for a natural-looking video-call mode.** No live publisher, camera
driver, virtual-camera registration, or device settings were changed.

Captured eight seconds, no audio: 59 unique illuminated Gray8 frames, 640x400,
7.489 fps measured from sensor timestamps (127.5–144.4 ms between frames).
The capture process exited successfully and released the sensor. No publisher
or capture-helper processes remained after testing.

## Results

The same clip was processed at batch size one with DDColor's official inference
pipeline, strict checkpoint loading, CUDA 12.8, PyTorch 2.7.1+cu128,
torchvision 0.22.1+cu128, OpenCV 4.12.0.88, NumPy 2.2.6 and FP16 autocast.
Original output stays 640x400; the sizes below are the model's square input.

| Model / input | Preprocessing | Mean ms/frame | p95 ms | Processing fps | Peak PyTorch allocated MiB |
| --- | --- | ---: | ---: | ---: | ---: |
| DDColor-Tiny / 256 | Original | 46.16 | 54.67 | 21.66 | 453 |
| DDColor-Tiny / 256 | Fixed gamma 0.6 | 44.08 | 51.48 | 22.69 | 452 |
| DDColor-Tiny / 512 | Original | 66.91 | 72.67 | 14.95 | 770 |
| DDColor-Tiny / 512 | Fixed gamma 0.6 | 68.85 | 79.15 | 14.52 | 771 |
| DDColor-L ModelScope / 512 | Original | 93.49 | 109.89 | 10.70 | 1773 |
| DDColor-L ModelScope / 512 | Fixed gamma 0.6 | 92.01 | 102.58 | 10.87 | 1773 |

These are warmed per-frame processing measurements including CPU preprocessing,
GPU transfers, inference and postprocessing. Disk writes, video encoding,
capture, Meet, and initial model loading are excluded. Five warmup predictions
per variant were excluded; CUDA was synchronized around each timed call.
GPU memory is peak tensor allocation, **not** total device/process consumption.
The largest observed frame time was 140.1 ms (large model, brightened input),
slightly above the source's approximately 133.5 ms frame interval.
This is not a live-call or sustained thermal/load test and does not establish
30-fps throughput or total call latency.

## Visual inspection

- Tiny/256: mostly weak coloration; faint skin/background tints.
- Tiny/512: stronger yellow/green patches and inconsistent color across sampled frames.
- Large/512: stronger but implausible orange/red skin and green/blue background casts.
- Fixed brightness lifting exposes more noise and does not resolve color fidelity.
- Bright IR eye reflections/highlights remain; adding chroma cannot recover detail
  missing from the source. No generative face reconstruction was attempted.

Outputs were inspected at representative points in the clip. No paired RGB
reference/ground truth was recorded, so these are artifact assessments, not
quantitative accuracy claims. DDColor is an image model trained for conventional
grayscale input, not this NIR sensor. No temporal smoothing was applied.
The recorded global chroma-change statistic includes scene motion and exposure
variation, and must not be reported as a measured flicker reduction.

## Artifacts and validation

Ignored local folder: `build/color-test-20260923/`.

- `raw/`: sensor PGM frames and `frames.csv` timestamps.
- `tiny256/`, `tiny512/`, `large512/`: `comparison.mp4`, `preview.png`, per-frame
  color outputs, and detailed `metrics.json`.
- Comparisons show original IR (top left), colorized original (top right),
  brightness-lifted IR (bottom left), and colorized brightened IR (bottom right).
- H.264 comparisons contain 59 frames at the measured average capture cadence,
  about 7.88 seconds. No audio. Playback averages the small timestamp jitter.
- Capture helper compiled successfully; capture and inference existing-output
  guards passed; Python syntax, PowerShell parse, pip dependency and Git whitespace
  checks passed. Generated videos were decoded with FFmpeg to check integrity.
- Footage, outputs, weights, virtual environment and Python bytecode are Git-ignored.
  Only experiment tools and this report are committed. No footage was uploaded.

Official source revision: `2adb63f2656ac41cbdf7b894cddd94121a3faf13`.
Tiny checkpoint SHA256: `8a1277bc90a1bfbb6d2d83933a9a6bc821931879ca93e26e4fcec12165d41fce`.
Large checkpoint SHA256: `d81711971ec59200da26d5e8a1afae8dd3778d495ea8ad7a7dadc769f403f7e7`.

Source: [official DDColor repository](https://github.com/piddnad/DDColor).
Models: [Tiny](https://huggingface.co/piddnad/ddcolor_paper_tiny),
[ModelScope](https://huggingface.co/piddnad/ddcolor_modelscope).

## Next experiment, not implemented

Evaluate reference-guided or NIR-trained colorization on consented paired RGB/IR
samples, plus motion-aware color stabilization. Require useful visual quality
before integrating into the publisher. Any future live worker should bound its
queue, drop outdated frames, and fall back to grayscale if processing fails.

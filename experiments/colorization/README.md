# Offline IR colorization experiment

This is **not** a change to UmbraCam's live publisher or installed virtual camera.
It captures eight seconds of IR-only video, closes the camera, and applies
DDColor-Tiny locally. No audio, uploads, reference-photo capture, driver changes,
or camera-setting writes are involved. Images and weights belong under ignored
`build/`, never in Git.

DDColor is an ordinary image colorizer, not a model trained on this IR sensor.
The purpose is to test feasibility and failure modes, not recover true colors.
The initial runner uses independent predictions. The optional experiments below
add temporal stabilization and person-first inference.

## Dependencies

- Existing UmbraCam MSVC/Windows SDK toolchain, FFmpeg on PATH, Python 3.12.
- An isolated `build/color-venv` environment (do not install into system Python).
- PyTorch 2.7.1 + torchvision 0.22.1, CUDA 12.8 wheels from the official PyTorch index.
- OpenCV 4.12.0.88 and NumPy 2.2.6.
- Official [DDColor](https://github.com/piddnad/DDColor) checkout in `build/DDColor`,
  tested revision `2adb63f2656ac41cbdf7b894cddd94121a3faf13`.
- Official [DDColor-Tiny weights and config](https://huggingface.co/piddnad/ddcolor_paper_tiny/tree/main),
  saved as `build/color-models/tiny.bin` and `tiny-config.json`.
  Weight SHA256: `8a1277bc90a1bfbb6d2d83933a9a6bc821931879ca93e26e4fcec12165d41fce`.
  Upstream source and model are Apache-2.0; neither is vendored in UmbraCam.

The same runner also supports the recommended DDColor-L ModelScope variant by
passing its own [weights/config](https://huggingface.co/piddnad/ddcolor_modelscope/tree/main).
Never mix one variant's config with another's checkpoint.

## Run from repository root

Close the UmbraCam publisher and other IR consumers first. Only capture when the
person at the computer is ready. Use fresh directories; existing output is rejected.

```powershell
.\experiments\colorization\build-capture.cmd
.\experiments\colorization\capture-clip.ps1 -OutputDirectory build\my-color-test\raw
.\build\color-venv\Scripts\python.exe experiments\colorization\test-clip.py `
  --source build\DDColor --weights build\color-models\tiny.bin `
  --config build\color-models\tiny-config.json --clip build\my-color-test\raw `
  --output build\my-color-test\result --size 256
```

The capture uses the same illuminated-frame policy as UmbraCam and saves unique
Gray8 PGM frames plus sensor timestamps. It discards two seconds for startup
settling, then saves eight seconds; initialization time is additional. When automating, wrap the capture process in a
25-second timeout and terminate only that child if it hangs.

Inference requires CUDA, uses restricted `weights_only=True` loading and checks
all model weights strictly. There is no silent CPU fallback. It tests unchanged
IR and a fixed gamma 0.6 brightness lift on exactly the same input clip. Both
variants preserve their input luminance through the official DDColor pipeline.

`comparison.mp4` shows four panels: original IR, raw-input colorization,
brightness-lifted IR, and brightness-lifted colorization. Playback uses the
measured average source frame rate (not claimed to be 30 fresh frames/sec).
`metrics.json` records CUDA/GPU versions, frame timings, weight hash, and peak
PyTorch allocated memory. Timing includes preprocessing, transfers, inference,
and postprocessing; excludes disk, encoding, camera capture, and the call app.
Five warmup predictions per variant are excluded. It is a short offline throughput
measurement, **not** a sustained/live-call latency benchmark.

No RGB ground truth is captured, so visual inspection can identify artifacts but
cannot establish color accuracy. The global chroma-step statistic is affected by
motion and is not a flicker/quality score.

## Motion-tracked color memory (second experiment)

`stabilize-clip.py` runs DDColor-L and torchvision RAFT-small on CUDA and compares
independent colors with reference-anchored color memory. This is still offline,
not a new live-camera mode. It uses the first captured frame's AI-generated
colors, **not** a real RGB reference or an asserted true-color image.

Download the official RAFT-small C_T_V2 checkpoint to `build/color-models/raft-small.pth`:
<https://download.pytorch.org/models/raft_small_C_T_V2-01064c6d.pth>.
SHA256: `01064c6dba73b0fc9fc8edf772248560a00a3acfd62ac6677e9eeebad9680e27`.
No runtime network access/download is performed by the experiment scripts.

```powershell
.\build\color-venv\Scripts\python.exe experiments\colorization\test_color_memory.py
.\build\color-venv\Scripts\python.exe experiments\colorization\stabilize-clip.py `
  --source build\DDColor --weights build\color-models\modelscope.bin `
  --config build\color-models\modelscope-config.json `
  --flow-weights build\color-models\raft-small.pth `
  --clip build\my-new-clip\raw --output build\my-new-clip\stable-result
```

The flow model tracks current-to-previous and current-to-reference motion, with
reverse flows to check consistency. Color reuse requires valid image coordinates,
forward/backward consistency, and similar image brightness. Trustworthy reference
colors are retained; other trackable pixels use 98% motion-warped previous color
and 2% current prediction. Newly revealed/unreliable pixels refresh from the
current model. A whole-scene mismatch resets the reference. All of these operations
affect only Lab color channels; luminance/detail always comes from the current IR
frame, so the method is not a frozen/blurred RGB-frame average.

Default flow resolution is 256x160 with six recurrent updates. Use
`--flow-width 320 --flow-updates 12` for the more expensive comparison. DDColor
uses 512x512, FP16 autocast; RAFT and warping use FP32 CUDA. No silent CPU fallback.
`comparison.mp4` contains IR / independent colors / color memory;
`stable.mp4` is the stabilized camera view alone.

The motion-aligned chroma metric uses the same flow/confidence for both outputs,
including a separate moving-pixel subset. It measures color changes, not true
color accuracy. It also favors this algorithm because it uses its own motion
estimates: inspect head turns, hands, occlusions and image edges for color trails.
Metrics record chroma strength so reduced variation cannot be quietly explained
as removing all color. This short test does not establish long-call stability,
performance under other GPU loads, or correctness on new faces/scenes.

## Person-first inference (third experiment)

Add `--person-weights build/color-models/person-deeplab.pth` to the stabilization
command to segment the person before colorization. Download the official
[DeepLabV3-MobileNetV3 checkpoint](https://download.pytorch.org/models/deeplabv3_mobilenet_v3_large-fc3c493d.pth)
to that path. SHA256:
`fc3c493d68e89cc31ef488c803d5d7dd2f3190fb570598faa49fef69be8e5e70`.
No extra Python packages are required beyond the pinned environment above.

The segmenter runs FP32 on CUDA, selects the largest person component, then
tracks a padded square crop. Background pixels are replaced with neutral gray
**before** DDColor sees the crop. The crop grows as needed and its center is
smoothed causally. Color memory is gated by both current and warped person masks.
The result is composited onto the original grayscale scene, with inward feathering
and a two-pixel boundary trim. Missing detection returns grayscale and clears the
crop tracker. All three networks require CUDA; mask cleanup uses CPU OpenCV.

```powershell
.\build\color-venv\Scripts\python.exe experiments\colorization\test_person_focus.py
.\build\color-venv\Scripts\python.exe experiments\colorization\stabilize-clip.py `
  --source build\DDColor --weights build\color-models\modelscope.bin `
  --config build\color-models\modelscope-config.json `
  --flow-weights build\color-models\raft-small.pth `
  --person-weights build\color-models\person-deeplab.pth `
  --clip build\my-new-clip\raw --output build\my-new-clip\person-result
.\build\color-venv\Scripts\python.exe experiments\colorization\compare-person.py `
  --clip build\my-new-clip\raw --full-frame build\my-new-clip\stable-result `
  --person build\my-new-clip\person-result --output build\my-new-clip\person-comparison
```

Use outputs from exactly the same raw clip for `compare-person.py`; it checks
frame counts and average frame rates, not source identity. Its comparison applies
the same silhouette to the old full-frame colors, so both variants have identical
grayscale backgrounds. Crop magnification also changes: this is not a pure
background-context ablation. `model-inputs/` shows what DDColor actually sees;
`masks/` saves the compositing matte. Background equality is verified on lossless
frames; H.264 encoding can introduce small changes near edges.

The segmenter was trained on visible-light images, not this IR camera. Multiple
people, disconnected hands and unusual poses can be missed. Crop scale only grows
until detection is lost; a long session may need a better scale tracker. Silhouette
jitter and colored internal edges remain possible. Neutralizing the background
does not recover missing visible colors or correct an initially wrong reference.
Person-mode motion metrics use only confident person pixels and cannot be compared
directly with full-frame metrics. Coverage fields still divide by the whole frame;
chroma-magnitude fields describe internal buffers, not just the displayed person.

This remains an offline experiment; see
[measured results](../../docs/person-color-test-2026-09-23.md). It is not installed
or enabled in Meet.

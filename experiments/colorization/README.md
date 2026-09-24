# Offline IR colorization experiment

This is **not** a change to UmbraCam's live publisher or installed virtual camera.
It captures eight seconds of IR-only video, closes the camera, and applies
DDColor-Tiny locally. No audio, uploads, reference-photo capture, driver changes,
or camera-setting writes are involved. Images and weights belong under ignored
`build/`, never in Git.

DDColor is an ordinary image colorizer, not a model trained on this IR sensor.
The purpose is to test feasibility and failure modes, not recover true colors.
Outputs are independent frame predictions; there is no temporal stabilization.

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
Gray8 PGM frames plus sensor timestamps. It stops after eight seconds of polling;
initialization time is additional. When automating, wrap the capture process in a
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

"""Offline DDColor baseline. No camera access, uploads, or virtual-camera changes.

Supply the official DDColor checkout and locally downloaded weights/config.
This script deliberately requires CUDA and strict weight loading so an accidental
CPU fallback or partly initialized model cannot masquerade as a successful test.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--weights", type=Path, required=True)
    parser.add_argument("--config", type=Path, required=True)
    parser.add_argument("--clip", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--size", type=int, choices=[256, 512], default=256)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Use a new output directory; existing results are never overwritten.")
    import cv2
    import numpy as np
    import torch
    sys.path.insert(0, str(args.source.resolve()))
    from ddcolor import DDColor, ColorizationPipeline

    if not torch.cuda.is_available():
        raise RuntimeError("CUDA unavailable; refusing silent CPU fallback.")
    cv2.setNumThreads(1)
    torch.set_num_threads(4)
    torch.backends.cudnn.benchmark = True
    config = json.loads(args.config.read_text())
    config["input_size"] = [args.size, args.size]
    model = DDColor(**config)
    state = torch.load(args.weights, map_location="cpu", weights_only=True)
    model.load_state_dict(state, strict=True)
    model = model.eval().to("cuda")
    pipeline = ColorizationPipeline(model, input_size=args.size, device=torch.device("cuda"))

    with (args.clip / "frames.csv").open(newline="") as handle:
        rows = list(csv.DictReader(handle))
    if len(rows) < 2:
        raise ValueError("Need at least two frames.")
    # Reject a malformed manifest rather than reading paths outside the clip.
    if any(Path(row["file"]).name != row["file"] or not row["file"].endswith(".pgm") for row in rows):
        raise ValueError("Invalid frame filenames.")
    frames = [cv2.imread(str(args.clip / row["file"]), cv2.IMREAD_COLOR) for row in rows]
    if any(frame is None for frame in frames):
        raise ValueError("Unreadable input frame.")
    if any(frame.shape != frames[0].shape for frame in frames):
        raise ValueError("Input dimensions changed.")
    stamps = np.array([int(row["time_100ns"]) for row in rows], dtype=np.int64)
    if not np.all(np.diff(stamps) > 0):
        raise ValueError("Non-increasing timestamps.")
    fps = (len(frames) - 1) * 1e7 / float(stamps[-1] - stamps[0])
    # Constant gamma, not frame-by-frame auto exposure; no temporal stabilization.
    lut = np.rint((np.arange(256, dtype=np.float32) / 255) ** 0.6 * 255).astype(np.uint8)
    adjusted = [cv2.LUT(frame, lut) for frame in frames]
    args.output.mkdir(parents=True)
    results = {}
    first_outputs = None
    for label, inputs in [("raw", frames), ("gamma06", adjusted)]:
        output_dir = args.output / label
        output_dir.mkdir()
        with torch.inference_mode(), torch.autocast("cuda", dtype=torch.float16):
            for _ in range(5):
                pipeline.process(inputs[0])
            torch.cuda.synchronize()
            torch.cuda.reset_peak_memory_stats()
            times, outputs = [], []
            for index, frame in enumerate(inputs):
                torch.cuda.synchronize()
                start = time.perf_counter()
                out = pipeline.process(frame)
                torch.cuda.synchronize()
                times.append((time.perf_counter() - start) * 1000)
                outputs.append(out)
                if not cv2.imwrite(str(output_dir / f"frame-{index:04d}.png"), out):
                    raise OSError("Failed to save output frame.")
        means_ab = np.array([cv2.cvtColor(x.astype(np.float32) / 255, cv2.COLOR_BGR2Lab)[:, :, 1:].mean(axis=(0, 1)) for x in outputs])
        results[label] = {
            "mean_ms": float(np.mean(times)), "median_ms": float(np.median(times)),
            "p95_ms": float(np.percentile(times, 95)), "max_ms": float(max(times)),
            "processing_fps": float(1000 / np.mean(times)),
            "peak_allocated_MiB": torch.cuda.max_memory_allocated() / 1024 ** 2,
            "mean_global_chroma_step": float(np.abs(np.diff(means_ab, axis=0)).mean()),
            "frame_times_ms": times,
        }
        print(label, json.dumps({k: v for k, v in results[label].items() if k != "frame_times_ms"}), flush=True)
        if first_outputs is None:
            first_outputs = outputs
        else:
            comparison = args.output / "comparison-frames"
            comparison.mkdir()
            h, w = frames[0].shape[:2]
            for index, (raw, raw_color, bright, color) in enumerate(zip(frames, first_outputs, adjusted, outputs)):
                panels = []
                for image, title in [(raw, "Original IR"), (raw_color, "AI color: original IR"), (bright, "IR: fixed brightness lift"), (color, "AI color: brightness lifted")]:
                    panel = cv2.copyMakeBorder(image, 36, 0, 0, 0, cv2.BORDER_CONSTANT)
                    cv2.putText(panel, title, (12, 25), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (255, 255, 255), 1, cv2.LINE_AA)
                    panels.append(panel)
                tiled = np.vstack((np.hstack(panels[:2]), np.hstack(panels[2:])))
                if not cv2.imwrite(str(comparison / f"frame-{index:04d}.png"), tiled):
                    raise OSError("Failed to save comparison.")
            preview = cv2.imread(str(comparison / f"frame-{len(frames)//2:04d}.png"))
            cv2.imwrite(str(args.output / "preview.png"), preview)
            subprocess.run([
                "ffmpeg", "-hide_banner", "-loglevel", "error", "-n", "-framerate", str(fps),
                "-i", str(comparison / "frame-%04d.png"), "-c:v", "libx264", "-crf", "18",
                "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(args.output / "comparison.mp4")
            ], check=True)
    with args.weights.open("rb") as weights_file:
        weights_hash = hashlib.file_digest(weights_file, "sha256").hexdigest()
    report = {
        "model": "DDColor", "encoder": config["encoder_name"], "source_revision": subprocess.check_output(["git", "-C", str(args.source), "rev-parse", "HEAD"], text=True).strip(),
        "weights_sha256": weights_hash,
        "gpu": torch.cuda.get_device_name(), "torch": torch.__version__, "cuda": torch.version.cuda,
        "precision": "autocast fp16", "model_input_size": args.size,
        "frames": len(frames), "frame_size": [w, h], "source_fps": fps,
        "source_frame_interval_min_max_ms": [float(np.diff(stamps).min()/1e4), float(np.diff(stamps).max()/1e4)],
        "timing": "Warm end-to-end colorization incl CPU preprocessing and GPU transfers; excludes disk I/O, encoding, capture, and Meet. Five warmup frames excluded per variant.",
        "limitations": "Independent image model, no temporal smoothing, no RGB reference or true-color ground truth. Chroma step includes motion, not a quality score. Video playback uses measured average frame rate.",
        "results": results,
    }
    (args.output / "metrics.json").write_text(json.dumps(report, indent=2))
    print("Saved comparison.mp4, preview.png, metrics.json to", args.output, flush=True)


if __name__ == "__main__":
    main()

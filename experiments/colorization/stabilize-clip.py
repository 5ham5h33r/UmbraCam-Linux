"""Causal local GPU color-memory experiment. Does not change the live publisher."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
import cv2
import numpy as np
import torch
from torch.nn import functional as F
from torchvision.models.optical_flow import raft_small
from color_memory import warp, reliable_match, blend_chroma


def tensor(image):
    return torch.from_numpy(np.ascontiguousarray(image.transpose(2, 0, 1))).unsqueeze(0).float().cuda()


def render(luminance, chroma):
    h, w = luminance.shape[:2]
    ab = F.interpolate(chroma, size=(h, w), mode="bilinear", align_corners=False)[0].permute(1, 2, 0).cpu().numpy()
    color = cv2.cvtColor(np.concatenate((luminance, ab), axis=2), cv2.COLOR_LAB2BGR)
    return np.clip(np.rint(color * 255), 0, 255).astype(np.uint8)


def digest(path):
    with path.open("rb") as handle:
        return hashlib.file_digest(handle, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("source", "weights", "config", "flow-weights", "clip", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    parser.add_argument("--flow-width", type=int, choices=[256, 320], default=256)
    parser.add_argument("--flow-updates", type=int, choices=[6, 12], default=6)
    parser.add_argument("--person-weights", type=Path, help="Enable person-first segmentation, crop and grayscale background.")
    args = parser.parse_args()
    flow_width, flow_height = args.flow_width, args.flow_width * 5 // 8
    if args.output.exists():
        parser.error("Choose a new output directory.")
    if not torch.cuda.is_available():
        raise RuntimeError("CUDA required; CPU fallback is disabled.")
    torch.set_num_threads(4)
    cv2.setNumThreads(1)
    torch.backends.cudnn.benchmark = True
    sys.path.insert(0, str(args.source.resolve()))
    from ddcolor import DDColor, ColorizationPipeline
    config = json.loads(args.config.read_text())
    config["input_size"] = [512, 512]
    model = DDColor(**config)
    model.load_state_dict(torch.load(args.weights, weights_only=True, map_location="cpu"), strict=True)
    colorizer = ColorizationPipeline(model.eval().cuda(), input_size=512, device=torch.device("cuda"))
    focus = None
    if args.person_weights:
        from person_focus import PersonFocus
        focus = PersonFocus(args.person_weights)
    motion = raft_small(weights=None).eval()
    motion.load_state_dict(torch.load(args.flow_weights, weights_only=True, map_location="cpu"), strict=True)
    motion = motion.cuda()
    assert next(model.parameters()).is_cuda and next(motion.parameters()).is_cuda
    with (args.clip / "frames.csv").open(newline="") as file:
        rows = list(csv.DictReader(file))
    if len(rows) < 2 or any(Path(r["file"]).name != r["file"] for r in rows):
        raise ValueError("Invalid clip manifest.")
    images = [cv2.imread(str(args.clip / r["file"])) for r in rows]
    if any(im is None for im in images) or any(im.shape != images[0].shape for im in images):
        raise ValueError("Missing frames or inconsistent dimensions.")
    stamps = np.array([int(r["time_100ns"]) for r in rows], dtype=np.int64)
    if not np.all(np.diff(stamps) > 0):
        raise ValueError("Non-increasing timestamps.")
    fps = (len(rows) - 1) * 1e7 / float(stamps[-1] - stamps[0])
    args.output.mkdir(parents=True)
    for name in ("baseline", "stable", "comparison-frames"):
        (args.output / name).mkdir()
    if focus:
        for name in ("masks", "model-inputs"):
            (args.output / name).mkdir()
    stats = []
    previous_gray = previous_ab = previous_baseline = reference_gray = reference_ab = None
    previous_alpha = reference_alpha = None
    last_detected = False
    resets = []
    with torch.inference_mode():
        first = tensor(cv2.resize(images[0], (flow_width, flow_height)).astype(np.float32) / 255)
        # Official RAFT input range [-1,1], dimensions divisible by eight.
        for _ in range(5):
            with torch.autocast("cuda", dtype=torch.float16):
                colorizer.process(images[0])
            if focus:
                focus.process(images[0], colorizer)
            # RAFT uses fp32 to avoid correlation/coordinate precision issues.
            motion(first.repeat(4, 1, 1, 1) * 2 - 1, first.repeat(4, 1, 1, 1) * 2 - 1, num_flow_updates=args.flow_updates)
        torch.cuda.synchronize()
        if focus:
            focus.tracker.reset() # Warmup must not change the causal tracking state.
        torch.cuda.reset_peak_memory_stats()
        for index, image in enumerate(images):
            torch.cuda.synchronize()
            started = time.perf_counter()
            focus_info = None
            if focus:
                prediction, alpha_numpy, focus_info = focus.process(image, colorizer)
                alpha = F.interpolate(tensor(alpha_numpy[:, :, None]), size=(flow_height, flow_width), mode="bilinear", align_corners=False)
            else:
                with torch.autocast("cuda", dtype=torch.float16):
                    prediction = colorizer.process(image)
            torch.cuda.synchronize()
            color_ms = (time.perf_counter() - started) * 1000
            gray = tensor(cv2.resize(image[:, :, :1], (flow_width, flow_height))[:, :, None].astype(np.float32) / 255)
            predicted_lab = cv2.cvtColor(prediction.astype(np.float32) / 255, cv2.COLOR_BGR2Lab)
            ab = F.interpolate(tensor(predicted_lab[:, :, 1:]), size=(flow_height, flow_width), mode="bilinear", align_corners=False)
            luminance = cv2.cvtColor(image.astype(np.float32) / 255, cv2.COLOR_BGR2Lab)[:, :, :1]
            previous_confidence = reference_confidence = torch.ones_like(gray)
            reset = index == 0
            if previous_gray is not None:
                # Batch both directions for adjacent frames and the fixed reference.
                sources = torch.cat((gray, previous_gray, gray, reference_gray)).repeat(1, 3, 1, 1) * 2 - 1
                targets = torch.cat((previous_gray, gray, reference_gray, gray)).repeat(1, 3, 1, 1) * 2 - 1
                flows = motion(sources, targets, num_flow_updates=args.flow_updates)[-1]
                if not torch.isfinite(flows).all():
                    raise RuntimeError("Non-finite motion estimate.")
                back, forward, ref_back, ref_forward = flows.split(1)
                previous_confidence, photo_error = reliable_match(gray, previous_gray, back, forward)
                reference_confidence, _ = reliable_match(gray, reference_gray, ref_back, ref_forward)
                # Whole-scene mismatch: don't drag old colors into a new scene.
                reset = previous_confidence.mean().item() < 0.25 or photo_error.mean().item() > 0.20
                if focus:
                    # Reuse colors only when both current and source pixels belong
                    # to the person. Do not anchor a moved face to old background.
                    old_mask, _ = warp(previous_alpha, back)
                    ref_mask, _ = warp(reference_alpha, ref_back)
                    previous_confidence *= (alpha > 0.8) * (old_mask > 0.8)
                    reference_confidence *= (alpha > 0.8) * (ref_mask > 0.8)
                    reset |= not focus_info["detected"] or not last_detected
            if reset:
                stable_ab = ab.clone()
                reference_gray, reference_ab = gray.clone(), ab.clone()
                if focus:
                    reference_alpha = alpha.clone()
                resets.append(index)
            else:
                stable_ab = blend_chroma(ab, previous_ab, reference_ab, back, ref_back,
                                         previous_confidence, reference_confidence)
            if not torch.isfinite(stable_ab).all():
                raise RuntimeError("Non-finite chroma.")
            baseline_image = render(luminance, ab)
            stable_image = render(luminance, stable_ab)
            if focus:
                # Composite in the original frame with the full-resolution inward
                # feather. Outside the silhouette stays byte-identical grayscale.
                matte = alpha_numpy[:, :, None]
                baseline_image = np.rint(baseline_image * matte + image * (1 - matte)).astype(np.uint8)
                stable_image = np.rint(stable_image * matte + image * (1 - matte)).astype(np.uint8)
                outside = alpha_numpy == 0
                if not np.array_equal(stable_image[outside], image[outside]):
                    raise RuntimeError("Background was unexpectedly colorized.")
            torch.cuda.synchronize()
            total_ms = (time.perf_counter() - started) * 1000
            row = {"frame": index, "color_ms": color_ms, "total_ms": total_ms,
                   "reference_coverage": reference_confidence.mean().item(),
                   "previous_coverage": previous_confidence.mean().item(), "reset": reset,
                   "baseline_chroma_magnitude": ab.abs().mean().item(),
                   "stable_chroma_magnitude": stable_ab.abs().mean().item()}
            if focus:
                row["person"] = focus_info
            if index and not reset:
                base_warp, _ = warp(previous_baseline, back)
                stable_warp, _ = warp(previous_ab, back)
                for suffix, mask in [("tracked", previous_confidence),
                                     ("moving", previous_confidence * (torch.linalg.vector_norm(back, dim=1, keepdim=True) > 0.75))]:
                    count = mask.sum().item()
                    row[suffix + "_pixels"] = count
                    if count:
                        row["baseline_" + suffix + "_chroma_error"] = ((ab - base_warp).abs() * mask).sum().item() / (2 * count)
                        row["stable_" + suffix + "_chroma_error"] = ((stable_ab - stable_warp).abs() * mask).sum().item() / (2 * count)
            previous_gray, previous_ab, previous_baseline = gray, stable_ab, ab
            if focus:
                previous_alpha = alpha
                last_detected = focus_info["detected"]
                if not cv2.imwrite(str(args.output / "masks" / f"frame-{index:04d}.png"), np.rint(alpha_numpy * 255).astype(np.uint8)):
                    raise OSError("Mask write failed.")
                if focus.last_input is not None:
                    if not cv2.imwrite(str(args.output / "model-inputs" / f"frame-{index:04d}.png"), focus.last_input):
                        raise OSError("Model input write failed.")
            stats.append(row)
            for folder, frame in [("baseline", baseline_image), ("stable", stable_image)]:
                if not cv2.imwrite(str(args.output / folder / f"frame-{index:04d}.png"), frame):
                    raise OSError("Frame write failed.")
            panels = []
            for frame, title in [(image, "Live IR"), (baseline_image, "Person crop: independent colors" if focus else "Independent AI colors"),
                                 (stable_image, "Person crop: stabilized" if focus else "Motion-tracked color memory")]:
                panel = cv2.copyMakeBorder(frame, 40, 0, 0, 0, cv2.BORDER_CONSTANT)
                cv2.putText(panel, title, (12, 27), cv2.FONT_HERSHEY_SIMPLEX, 0.65, (255, 255, 255), 1, cv2.LINE_AA)
                panels.append(panel)
            comparison = np.hstack(panels)
            if not cv2.imwrite(str(args.output / "comparison-frames" / f"frame-{index:04d}.png"), comparison):
                raise OSError("Comparison write failed.")
            if index == len(images) // 2:
                cv2.imwrite(str(args.output / "preview.png"), comparison)
            if index % 10 == 0:
                print(f"{index + 1}/{len(images)} frames; {total_ms:.1f} ms; reference coverage {row['reference_coverage']:.1%}", flush=True)
    aggregate = {}
    for region in ("tracked", "moving"):
        samples = [r for r in stats if r.get(region + "_pixels", 0) > 0]
        pixels = sum(r[region + "_pixels"] for r in samples)
        if pixels:
            for variant in ("baseline", "stable"):
                key = variant + "_" + region + "_chroma_error"
                aggregate[key] = sum(r[key] * r[region + "_pixels"] for r in samples) / pixels
            aggregate[region + "_reduction_percent"] = 100 * (1 - aggregate["stable_" + region + "_chroma_error"] / max(aggregate["baseline_" + region + "_chroma_error"], 1e-8))
    timings = [r["total_ms"] for r in stats[1:]]
    report = {"gpu": torch.cuda.get_device_name(), "torch": torch.__version__, "cuda": torch.version.cuda,
              "color_model": config["encoder_name"], "color_precision": "FP16 autocast",
              "flow_model": "torchvision RAFT-small C_T_V2", "flow_precision": "FP32 CUDA",
              "flow_size": [flow_width, flow_height], "flow_updates": args.flow_updates,
              "person_weights_sha256": digest(args.person_weights) if focus else None,
              "person_mode": "CUDA DeepLabV3-MobileNet, person-first neutralized crop, original grayscale background" if focus else None,
              "color_ms_includes_person_segmentation": bool(focus),
              "source_fps": fps, "frames": len(images), "color_weights_sha256": digest(args.weights),
              "flow_weights_sha256": digest(args.flow_weights), "resets": resets,
              "mean_total_ms": float(np.mean(timings)), "p95_total_ms": float(np.percentile(timings, 95)),
              "peak_allocated_MiB": torch.cuda.max_memory_allocated() / 1024 ** 2,
              "metrics": aggregate, "per_frame": stats,
              "limitations": "Offline short-clip test; no true-color ground truth. Motion metric uses the algorithm's own flow and confidence, not an independent perceptual measure. Lower error does not prove absence of ghosting. Timing includes both rendered outputs, excludes evaluation metrics, disk I/O, encoding, capture and Meet. First bootstrap frame excluded from timing summary. Luminance is always the current IR frame; no texture/frame freezing."}
    (args.output / "metrics.json").write_text(json.dumps(report, indent=2))
    for folder, name in [("comparison-frames", "comparison.mp4"), ("stable", "stable.mp4")]:
        subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-n", "-framerate", str(fps),
                        "-i", str(args.output / folder / "frame-%04d.png"), "-c:v", "libx264", "-crf", "18",
                        "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(args.output / name)], check=True)
    print(json.dumps({k: v for k, v in report.items() if k != "per_frame"}, indent=2), flush=True)


if __name__ == "__main__":
    main()

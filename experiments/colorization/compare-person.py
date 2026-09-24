"""Compare already-rendered same-clip outputs with the same grayscale background.

This separates person-first inference from merely masking a full-frame colorizer
afterwards. Crop magnification still differs, so this is not a pure background-
influence ablation. No additional inference or camera access occurs here.
"""
import argparse
import csv
import json
from pathlib import Path
import subprocess
import cv2
import numpy as np


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("clip", "full-frame", "person", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    if args.output.exists():
        parser.error("Choose a new output directory.")
    with (args.clip / "frames.csv").open(newline="") as file:
        rows = list(csv.DictReader(file))
    if len(rows) < 2 or any(Path(row["file"]).name != row["file"] for row in rows):
        raise ValueError("Invalid manifest.")
    full_report = json.loads((args.full_frame / "metrics.json").read_text())
    person_report = json.loads((args.person / "metrics.json").read_text())
    if full_report["frames"] != len(rows) or person_report["frames"] != len(rows):
        raise ValueError("Frame count mismatch.")
    fps = (len(rows) - 1) * 1e7 / (int(rows[-1]["time_100ns"]) - int(rows[0]["time_100ns"]))
    if abs(full_report["source_fps"] - fps) > 1e-6 or abs(person_report["source_fps"] - fps) > 1e-6:
        raise ValueError("Source frame rate mismatch.")
    args.output.mkdir(parents=True)
    for index, row in enumerate(rows):
        raw = cv2.imread(str(args.clip / row["file"]))
        full = cv2.imread(str(args.full_frame / "stable" / f"frame-{index:04d}.png"))
        focused = cv2.imread(str(args.person / "stable" / f"frame-{index:04d}.png"))
        mask = cv2.imread(str(args.person / "masks" / f"frame-{index:04d}.png"), cv2.IMREAD_GRAYSCALE)
        if any(x is None for x in (raw, full, focused, mask)):
            raise ValueError("Missing input.")
        if raw.shape != full.shape or raw.shape != focused.shape or raw.shape[:2] != mask.shape:
            raise ValueError("Shape mismatch.")
        matte = mask[:, :, None].astype(np.float32) / 255
        full = np.rint(full * matte + raw * (1 - matte)).astype(np.uint8)
        if not np.array_equal(focused[mask == 0], raw[mask == 0]):
            raise ValueError("Person output modified background pixels.")
        panels = []
        for image, title in [(raw, "Original IR"), (full, "Full-frame AI, masked afterwards"),
                             (focused, "Person-first AI + color memory")]:
            panel = cv2.copyMakeBorder(image, 40, 0, 0, 0, cv2.BORDER_CONSTANT)
            cv2.putText(panel, title, (10, 27), cv2.FONT_HERSHEY_SIMPLEX, 0.62, (255, 255, 255), 1, cv2.LINE_AA)
            panels.append(panel)
        composite = np.hstack(panels)
        if not cv2.imwrite(str(args.output / f"frame-{index:04d}.png"), composite):
            raise OSError("Comparison write failed.")
        if index == len(rows) // 2:
            cv2.imwrite(str(args.output / "preview.png"), composite)
    subprocess.run(["ffmpeg", "-hide_banner", "-loglevel", "error", "-n", "-framerate", str(fps),
                    "-i", str(args.output / "frame-%04d.png"), "-c:v", "libx264", "-crf", "18",
                    "-pix_fmt", "yuv420p", "-movflags", "+faststart", str(args.output / "comparison.mp4")], check=True)
    print(f"Compared {len(rows)} frames. Same-mask background equality: PASS.")


if __name__ == "__main__":
    main()

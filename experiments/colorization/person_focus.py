"""GPU person segmentation and causal, padded crops for an offline IR experiment.

Background is neutralized BEFORE color inference. Segmentation is trained on
visible-light images; confident-looking IR mistakes remain possible. If no
usable silhouette is found, return grayscale rather than a stale person mask.
"""
import cv2
import numpy as np
import torch
from torch.nn import functional as F
from torchvision.models.segmentation import deeplabv3_mobilenet_v3_large


def silhouette(probability, threshold=0.5):
    binary = (probability >= threshold).astype(np.uint8)
    count, labels, stats, _ = cv2.connectedComponentsWithStats(binary, 8)
    if count < 2:
        return np.zeros_like(probability, dtype=np.float32)
    selected = 1 + int(np.argmax(stats[1:, cv2.CC_STAT_AREA]))
    if stats[selected, cv2.CC_STAT_AREA] < probability.size * 0.005:
        return np.zeros_like(probability, dtype=np.float32)
    binary = (labels == selected).astype(np.uint8)
    binary = cv2.morphologyEx(binary, cv2.MORPH_CLOSE, np.ones((3, 3), np.uint8))
    # Feather inward only: never add a halo outside the selected silhouette.
    return np.minimum(cv2.distanceTransform(binary, cv2.DIST_L2, 3) / 3.0, 1).astype(np.float32)


def compositing_matte(alpha):
    # Trim two uncertain boundary pixels. This deliberately leaves a narrow
    # grayscale hair edge rather than painting a purple/blue model halo outside it.
    return cv2.erode(alpha, np.ones((5, 5), np.uint8), borderType=cv2.BORDER_CONSTANT, borderValue=0)


class CropTracker:
    def __init__(self):
        self.center = None
        self.side = 0

    def reset(self):
        self.center = None
        self.side = 0

    def update(self, alpha):
        ys, xs = np.where(alpha > 0)
        if len(xs) == 0:
            self.reset()
            return None
        left, right, top, bottom = int(xs.min()), int(xs.max()) + 1, int(ys.min()), int(ys.max()) + 1
        center = np.array([(left + right) / 2, (top + bottom) / 2], dtype=np.float32)
        # Grow immediately, don't shrink back and forth with noisy hair edges.
        needed = max(160, int(np.ceil(max(right - left, bottom - top) * 1.3 / 16)) * 16)
        self.side = max(self.side, needed)
        self.center = center if self.center is None else 0.75 * self.center + 0.25 * center
        half = self.side / 2
        # Keep the complete newly detected person even when the smoothed center lags.
        self.center[0] = np.clip(self.center[0], right - half, left + half)
        self.center[1] = np.clip(self.center[1], bottom - half, top + half)
        x, y = np.floor(self.center - half).astype(int)
        return int(x), int(y), self.side


def isolated_crop(image, alpha, box):
    x, y, side = box
    height, width = image.shape[:2]
    canvas = np.full((side, side, 3), 128, dtype=np.uint8)
    x0, y0, x1, y1 = max(x, 0), max(y, 0), min(x + side, width), min(y + side, height)
    if x0 < x1 and y0 < y1:
        mask = alpha[y0:y1, x0:x1, None]
        source = image[y0:y1, x0:x1].astype(np.float32)
        canvas[y0-y:y1-y, x0-x:x1-x] = np.rint(source * mask + 128 * (1 - mask)).astype(np.uint8)
    return canvas


def paste_prediction(image, prediction, box):
    """Place crop output in full-frame coordinates; silhouette is applied later."""
    x, y, side = box
    height, width = image.shape[:2]
    output = image.copy()
    x0, y0, x1, y1 = max(x, 0), max(y, 0), min(x + side, width), min(y + side, height)
    if x0 < x1 and y0 < y1:
        output[y0:y1, x0:x1] = prediction[y0-y:y1-y, x0-x:x1-x]
    return output


class PersonFocus:
    def __init__(self, weights):
        if not torch.cuda.is_available():
            raise RuntimeError("CUDA required for person segmentation.")
        self.model = deeplabv3_mobilenet_v3_large(weights=None, weights_backbone=None, aux_loss=True)
        self.model.load_state_dict(torch.load(weights, map_location="cpu", weights_only=True), strict=True)
        self.model = self.model.eval().cuda()
        self.mean = torch.tensor([0.485, 0.456, 0.406], device="cuda")[None, :, None, None]
        self.std = torch.tensor([0.229, 0.224, 0.225], device="cuda")[None, :, None, None]
        self.tracker = CropTracker()
        self.alpha = None
        self.box = None
        self.last_input = None

    @torch.inference_mode()
    def detect(self, image):
        height, width = image.shape[:2]
        rgb = np.ascontiguousarray(image[:, :, ::-1].transpose(2, 0, 1))
        value = torch.from_numpy(rgb).unsqueeze(0).float().cuda() / 255
        value = F.interpolate(value, size=(520, round(width * 520 / height)), mode="bilinear", align_corners=False, antialias=True)
        logits = self.model((value - self.mean) / self.std)["out"]
        logits = F.interpolate(logits, size=(height, width), mode="bilinear", align_corners=False)
        # VOC class 15 is person; retain only the largest connected person region.
        probability = logits.softmax(dim=1)[0, 15].cpu().numpy()
        self.alpha = silhouette(probability)
        self.box = self.tracker.update(self.alpha)
        return self.alpha, self.box, float(probability.max())

    def process(self, image, colorizer):
        alpha, box, confidence = self.detect(image)
        if box is None:
            self.last_input = None
            return image.copy(), alpha, {"detected": False, "max_probability": confidence, "box": None}
        self.last_input = isolated_crop(image, alpha, box)
        with torch.autocast("cuda", dtype=torch.float16):
            crop_color = colorizer.process(self.last_input)
        output_alpha = compositing_matte(alpha)
        return paste_prediction(image, crop_color, box), output_alpha, {
            "detected": True, "max_probability": confidence, "box": list(box),
            "silhouette_fraction": float((output_alpha > 0).mean()),
        }

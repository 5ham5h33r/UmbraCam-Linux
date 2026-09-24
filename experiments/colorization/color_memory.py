"""Motion-compensated chroma memory; deliberately never stores output luminance.

Flows are target-to-source pixel displacements. Reference anchoring prevents a
sequence of tiny per-frame hue changes from becoming large long-term drift.
Only valid, forward/backward-consistent, photometrically similar matches reuse
colors. Newly revealed regions use the current color prediction instead.
"""
import torch
from torch.nn import functional as F


def warp(source, backward_flow):
    """Return source sampled at target + backward_flow, and an in-bounds mask."""
    _, _, h, w = backward_flow.shape
    if source.shape[-2:] != (h, w) or min(h, w) < 2:
        raise ValueError("Source and flow must share spatial dimensions >= 2.")
    y, x = torch.meshgrid(torch.arange(h, device=source.device),
                          torch.arange(w, device=source.device), indexing="ij")
    sx = x.float()[None] + backward_flow[:, 0]
    sy = y.float()[None] + backward_flow[:, 1]
    valid = ((sx >= 0) & (sx <= w - 1) & (sy >= 0) & (sy <= h - 1))[:, None]
    grid = torch.stack((2 * sx / (w - 1) - 1, 2 * sy / (h - 1) - 1), dim=-1)
    result = F.grid_sample(source.float(), grid, mode="bilinear", padding_mode="border", align_corners=True)
    return result, valid


def reliable_match(current_gray, source_gray, backward, forward):
    source_warped, valid = warp(source_gray, backward)
    reverse_warped, _ = warp(forward, backward)
    consistency = torch.linalg.vector_norm(backward + reverse_warped, dim=1, keepdim=True)
    photo_error = (current_gray - source_warped).abs()
    trusted = valid & (consistency < 1.5) & (photo_error < 0.10)
    # Remove uncertain edge pixels rather than bleeding color across occlusions.
    trusted = -F.max_pool2d(-trusted.float(), 3, stride=1, padding=1)
    return trusted, photo_error


def blend_chroma(prediction, previous, reference, previous_flow, reference_flow,
                 previous_confidence, reference_confidence):
    """Keep anchored color where reliable, previous color otherwise, else refresh.

    The reference is authoritative on its matched pixels; there is no cumulative
    random recoloring there. A 2% update elsewhere lets memory adapt slowly.
    These are color channels only: each output uses the current live frame's L.
    """
    old, old_valid = warp(previous, previous_flow)
    anchor, anchor_valid = warp(reference, reference_flow)
    old_weight = previous_confidence * old_valid.float() * 0.98
    anchor_weight = reference_confidence * anchor_valid.float()
    temporal = old_weight * old + (1 - old_weight) * prediction
    return anchor_weight * anchor + (1 - anchor_weight) * temporal

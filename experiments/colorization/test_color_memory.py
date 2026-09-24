"""Small CUDA tests for warping, disocclusion and reference-color retention."""
import unittest
import torch
from color_memory import warp, reliable_match, blend_chroma


@unittest.skipUnless(torch.cuda.is_available(), "CUDA required")
class ColorMemoryTests(unittest.TestCase):
    def setUp(self):
        self.flow = torch.zeros(1, 2, 16, 24, device="cuda")
        self.gray = torch.rand(1, 1, 16, 24, device="cuda")
        self.ones = torch.ones_like(self.gray)

    def test_identity(self):
        value, valid = warp(self.gray, self.flow)
        torch.testing.assert_close(value, self.gray, atol=2e-6, rtol=1e-5)
        self.assertTrue(valid.all())

    def test_translation_direction_and_border(self):
        self.flow[:, 0] = -2
        value, valid = warp(self.gray, self.flow)
        torch.testing.assert_close(value[..., 2:], self.gray[..., :-2], atol=2e-6, rtol=1e-5)
        self.assertFalse(valid[..., :2].any())

    def test_photometric_failure(self):
        mask, _ = reliable_match(self.gray, self.gray + 0.5, self.flow, self.flow)
        self.assertFalse(mask.any())

    def test_forward_backward_failure(self):
        mask, _ = reliable_match(self.gray, self.gray, self.flow, self.flow + 4)
        self.assertFalse(mask.any())

    def test_anchor_prevents_cumulative_color_drift(self):
        anchor = torch.ones_like(self.flow) * 12
        previous = anchor.clone()
        for _ in range(100):
            previous = blend_chroma(anchor * -2, previous, anchor, self.flow, self.flow, self.ones, self.ones)
        torch.testing.assert_close(previous, anchor)

    def test_disocclusion_refreshes_instead_of_smearing(self):
        fresh = torch.ones_like(self.flow) * -8
        actual = blend_chroma(fresh, fresh + 20, fresh + 40, self.flow, self.flow, self.ones * 0, self.ones * 0)
        torch.testing.assert_close(actual, fresh)


if __name__ == "__main__":
    unittest.main()

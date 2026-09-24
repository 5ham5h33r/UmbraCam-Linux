import unittest
import numpy as np
from person_focus import CropTracker, isolated_crop, paste_prediction, silhouette, compositing_matte


class PersonFocusTests(unittest.TestCase):
    def test_background_changes_cannot_change_model_input(self):
        alpha = np.zeros((80, 100), np.float32)
        alpha[20:70, 40:70] = 1
        first = np.full((80, 100, 3), 20, np.uint8)
        second = np.full_like(first, 240)
        first[alpha > 0] = 100
        second[alpha > 0] = 100
        box = CropTracker().update(alpha)
        np.testing.assert_array_equal(isolated_crop(first, alpha, box), isolated_crop(second, alpha, box))

    def test_no_person_clears_tracker(self):
        tracker = CropTracker()
        tracker.update(np.ones((80, 100), np.float32))
        self.assertIsNone(tracker.update(np.zeros((80, 100), np.float32)))
        self.assertIsNone(tracker.center)

    def test_padded_crop_and_paste_coordinates(self):
        image = np.zeros((80, 100, 3), np.uint8)
        alpha = np.zeros((80, 100), np.float32)
        alpha[60:, :10] = 1
        image[alpha > 0] = 80
        box = CropTracker().update(alpha)
        crop = isolated_crop(image, alpha, box)
        restored = paste_prediction(image, crop, box)
        np.testing.assert_array_equal(restored[alpha > 0], image[alpha > 0])
        self.assertEqual(crop.shape, (box[2], box[2], 3))

    def test_tracker_contains_subject_and_does_not_shrink(self):
        tracker = CropTracker()
        alpha = np.zeros((400, 640), np.float32)
        alpha[100:300, 200:350] = 1
        box1 = tracker.update(alpha)
        alpha[:] = 0
        alpha[150:310, 400:500] = 1
        x, y, side = tracker.update(alpha)
        self.assertGreaterEqual(side, box1[2])
        self.assertLessEqual(x, 400)
        self.assertGreaterEqual(x + side, 500)
        self.assertLessEqual(y, 150)
        self.assertGreaterEqual(y + side, 310)

    def test_silhouette_rejects_small_false_region(self):
        probability = np.zeros((100, 100), np.float32)
        probability[0, 0] = 1
        self.assertFalse(silhouette(probability).any())

    def test_feather_does_not_extend_outside_silhouette(self):
        probability = np.zeros((100, 100), np.float32)
        probability[20:80, 30:70] = 1
        alpha = silhouette(probability)
        self.assertTrue(np.all(alpha[probability == 0] == 0))
        self.assertEqual(alpha[50, 50], 1)

    def test_composite_trims_uncertain_edges_without_expanding_mask(self):
        alpha = np.zeros((100, 100), np.float32)
        alpha[20:80, 30:70] = 1
        matte = compositing_matte(alpha)
        self.assertTrue(np.all(matte <= alpha))
        self.assertEqual(matte[20, 50], 0)
        self.assertEqual(matte[50, 50], 1)


if __name__ == "__main__":
    unittest.main()

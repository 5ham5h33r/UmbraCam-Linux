import io
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "linux"))
from ir_frames import FrameRelay, IrFramePolicy, stop_process, write_frame


class FramePolicyTests(unittest.TestCase):
    def test_alternating_noisy_dark_exposures_hold_the_bright_frame(self):
        policy = IrFramePolicy()
        bright = bytes([32]) * 256
        dark = bytes([0] * 255 + [90])
        self.assertEqual(policy.apply(bright, 0), bright)
        for index in range(1, 90):
            frame = dark if index % 2 else bright
            self.assertEqual(policy.apply(frame, index / 15), bright)
        self.assertEqual(policy.held, 45)

    def test_irregular_dark_bursts_do_not_depend_on_even_odd_phase(self):
        policy = IrFramePolicy()
        bright = bytes([40]) * 256
        dark = bytes([1]) * 256
        for index, frame in enumerate([bright, dark, dark, bright, bright, dark, dark, dark]):
            self.assertEqual(policy.apply(frame, index / 15), bright)

    def test_sustained_darkness_replaces_old_scene_after_one_second(self):
        policy = IrFramePolicy()
        bright, dark = bytes([40]) * 256, bytes([1]) * 256
        policy.apply(bright, 0)
        self.assertEqual(policy.apply(dark, 0.99), bright)
        self.assertEqual(policy.apply(dark, 1.0), dark)
        self.assertEqual(policy.apply(dark, 1.1), dark)
        self.assertEqual(policy.apply(bright, 1.2), bright)
        self.assertEqual(policy.apply(dark, 1.3), bright)

    def test_dark_only_and_first_dark_frame_pass_through(self):
        policy = IrFramePolicy()
        for index in range(40):
            dark = bytes([index % 2]) * 256
            self.assertEqual(policy.apply(dark, index / 15), dark)

    def test_normal_motion_is_not_blended(self):
        policy = IrFramePolicy()
        for index in range(30):
            frame = bytes(range(32, 64)) * 8 if index % 2 else bytes(range(48, 80)) * 8
            self.assertIs(policy.apply(frame, index / 15), frame)

    def test_moderate_lighting_changes_pass_through(self):
        policy = IrFramePolicy()
        for index, level in enumerate((40, 35, 30, 25, 30, 40)):
            frame = bytes([level]) * 256
            self.assertEqual(policy.apply(frame, index / 15), frame)

    def test_empty_frames_are_rejected(self):
        with self.assertRaises(ValueError):
            IrFramePolicy().apply(b"", 0)


class RelayTests(unittest.TestCase):
    def test_short_writes_preserve_exact_frame_bytes(self):
        class ShortWriter(io.BytesIO):
            def write(self, value):
                return super().write(value[:3])
        stream = ShortWriter()
        frame = bytes(range(32))
        write_frame(stream, frame)
        self.assertEqual(stream.getvalue(), frame)

    def test_relay_filters_complete_frames_through_real_pipes(self):
        class Destination(io.BytesIO):
            def close(self):
                self.result = self.getvalue()
                super().close()
        destination = Destination()
        relay = FrameRelay([sys.executable, "-c",
                            "import sys; sys.stdout.buffer.write(bytes([40])*256+bytes([1])*256)"],
                           destination, 256)
        relay.worker.join(timeout=5)
        relay.stop_capture()
        relay.close()
        self.assertIsNone(relay.error)
        self.assertEqual(destination.result, bytes([40]) * 512)

    def test_shutdown_releases_a_blocked_frame_writer(self):
        import subprocess
        destination = subprocess.Popen([sys.executable, "-c", "import time; time.sleep(30)"],
                                       stdin=subprocess.PIPE, bufsize=0)
        relay = FrameRelay([sys.executable, "-c",
                            "import sys; sys.stdout.buffer.write(bytes([40])*4000000)"],
                           destination.stdin, 256000)
        try:
            relay.stop_capture()
            stop_process(destination)
            relay.close()
            self.assertFalse(relay.worker.is_alive())
            self.assertIsNotNone(relay.capture.poll())
            self.assertIsNotNone(destination.poll())
        finally:
            relay.stop_capture()
            stop_process(destination)

    def test_truncated_frame_is_reported(self):
        class Destination(io.BytesIO):
            pass
        relay = FrameRelay([sys.executable, "-c", "import sys; sys.stdout.buffer.write(b'x')"],
                           Destination(), 256)
        relay.worker.join(timeout=5)
        relay.stop_capture()
        relay.close()
        self.assertIn("incomplete frame", relay.error)


if __name__ == "__main__":
    unittest.main()

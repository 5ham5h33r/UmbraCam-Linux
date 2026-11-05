import errno
import importlib.util
from pathlib import Path
import struct
import sys
import unittest
from unittest.mock import patch, mock_open

MODULE = Path(__file__).resolve().parents[2] / "linux" / "umbracam.py"
sys.path.insert(0, str(MODULE.parent))
spec = importlib.util.spec_from_file_location("umbracam", MODULE)
app = importlib.util.module_from_spec(spec)
sys.modules[spec.name] = app
spec.loader.exec_module(app)


def camera(path="/dev/video2", formats=("GREY",), caps=app.CAP_CAPTURE):
    return app.Device(path, "USB IR Camera", "uvcvideo", caps, formats)


def virtual(path="/dev/video10", name=app.LABEL):
    return app.Device(path, name, "v4l2 loopback", 2, ())


class DiscoveryTests(unittest.TestCase):
    def test_skips_rgb_metadata_and_virtual(self):
        ir = camera()
        found = [camera("/dev/video0", ("MJPG", "YUYV")),
                 camera("/dev/video1", (), 0x00800000), virtual(), ir]
        self.assertEqual(app.choose_source(found), ir)

    def test_no_ir_does_not_fall_back_to_rgb(self):
        with self.assertRaisesRegex(ValueError, "No GREY"):
            app.choose_source([camera(formats=("MJPG",))])

    def test_ambiguous_ir_requires_explicit_choice(self):
        with self.assertRaisesRegex(ValueError, "Multiple"):
            app.choose_source([camera(), camera("/dev/video4")])

    def test_metadata_with_grey_is_not_capture(self):
        self.assertFalse(camera(caps=0x00800000).infrared)

    def test_explicit_source_must_be_grayscale(self):
        with patch.object(app, "inspect_device", return_value=camera(formats=("YUYV",))):
            with self.assertRaises(ValueError):
                app.choose_source([], "/dev/video0")

    def test_output_must_be_dedicated_loopback(self):
        self.assertEqual(app.choose_output([virtual()]).path, "/dev/video10")
        for wrong in (camera(), virtual(name="OBS Virtual Camera")):
            with patch.object(app, "inspect_device", return_value=wrong):
                with self.assertRaises(ValueError):
                    app.choose_output([], wrong.path)

    def test_missing_or_ambiguous_output_fails(self):
        for found in ([], [virtual(), virtual("/dev/video11")]):
            with self.assertRaises(ValueError):
                app.choose_output(found)

    def test_device_caps_override_global_caps(self):
        def ioctl(_fd, request, buffer):
            if request == app.QUERYCAP:
                struct.pack_into("=16s32s32sIII", buffer, 0, b"uvcvideo", b"IR", b"usb",
                                 1, app.CAP_DEVICE_CAPS | app.CAP_CAPTURE, 0x00800000)
            else:
                self.fail("Metadata node must not enumerate capture formats")
        with patch("builtins.open", mock_open()), patch.object(app.fcntl, "ioctl", side_effect=ioctl):
            self.assertFalse(app.inspect_device("/dev/video1").infrared)

    def test_ioctl_grey_format_and_end_of_enumeration(self):
        def ioctl(_fd, request, buffer):
            if request == app.QUERYCAP:
                struct.pack_into("=16s32s32sIII", buffer, 0, b"uvcvideo", b"IR", b"usb",
                                 1, app.CAP_DEVICE_CAPS, app.CAP_CAPTURE)
            elif struct.unpack_from("=I", buffer)[0] == 0:
                buffer[44:48] = b"GREY"
            else:
                raise OSError(errno.EINVAL, "End of formats")
        with patch("builtins.open", mock_open()), patch.object(app.fcntl, "ioctl", side_effect=ioctl):
            self.assertTrue(app.inspect_device("/dev/video2").infrared)

    def test_unexpected_ioctl_errors_are_not_hidden(self):
        def ioctl(_fd, request, buffer):
            if request == app.QUERYCAP:
                struct.pack_into("=16s32s32sIII", buffer, 0, b"uvcvideo", b"IR", b"usb", 1,
                                 app.CAP_CAPTURE, app.CAP_CAPTURE)
            else:
                raise OSError(errno.EIO, "Disconnected")
        with patch("builtins.open", mock_open()), patch.object(app.fcntl, "ioctl", side_effect=ioctl):
            with self.assertRaises(OSError):
                app.inspect_device("/dev/video2")


class CaptureTests(unittest.TestCase):
    def test_pipeline_uses_gray_input_and_browser_compatible_output(self):
        args = app.parse_args(["run", "--all-ir-frames"])
        cmd = app.publish_command(camera(), virtual(), args)
        self.assertEqual(cmd[cmd.index("-input_format") + 1], "gray")
        self.assertEqual(cmd[cmd.index("-pix_fmt") + 1], "yuyv422")
        self.assertEqual(cmd[-1], "/dev/video10")
        self.assertNotIn("shell", cmd)

    def test_default_publisher_receives_filtered_raw_frames(self):
        command = app.publish_command(camera(), virtual(), app.parse_args(["run"]))
        self.assertEqual(command[command.index("-i") + 1], "pipe:0")
        self.assertEqual(command[command.index("-pixel_format") + 1], "gray")
        self.assertNotIn(camera().path, command)

    def test_bypass_option_reaches_background_publisher(self):
        with patch.object(app, "require"), patch.object(app.subprocess, "run") as run:
            run.return_value.returncode = 0
            app.service(app.parse_args(["enable", "--all-ir-frames"]))
            self.assertIn("--all-ir-frames", run.call_args.args[0])

    def test_same_device_is_rejected(self):
        with self.assertRaises(ValueError):
            app.publish_command(camera(), virtual("/dev/video2"), app.parse_args(["run"]))

    def test_stop_escalates_only_its_own_child(self):
        from unittest.mock import Mock
        child = Mock()
        child.poll.return_value = None
        child.wait.side_effect = [app.subprocess.TimeoutExpired("ffmpeg", 3), 0]
        app.stop_child(child)
        child.terminate.assert_called_once()
        child.kill.assert_called_once()

    def test_invalid_dimensions_and_rate_fail(self):
        for options in (["--size", "0x400"], ["--size", "640x400;true"], ["--fps", "0"]):
            with patch("sys.stderr"), self.assertRaises(SystemExit):
                app.parse_args(["run"] + options)

    def test_enable_waits_for_actual_frames_before_success(self):
        with patch.object(app, "require"), patch.object(app.subprocess, "run") as run:
            run.return_value.returncode = 0
            self.assertEqual(app.service(app.parse_args(["enable"])), 0)
            command = run.call_args.args[0]
            self.assertIn("--property=Type=notify", command)
            self.assertIn("--property=KillMode=control-group", command)
            self.assertIn("--collect", command)

    def test_no_frame_probe_is_failure(self):
        with patch.object(app, "require"), patch.object(app, "devices", return_value=[camera()]), \
             patch.object(app.subprocess, "run") as run:
            run.return_value.stdout = b""
            with self.assertRaisesRegex(ValueError, "Expected 15"):
                app.probe(app.parse_args(["probe"]))


if __name__ == "__main__":
    unittest.main()

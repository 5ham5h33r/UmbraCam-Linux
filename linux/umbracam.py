#!/usr/bin/env python3
"""Publish a Linux grayscale camera as a standard V4L2 webcam."""

import argparse
import errno
import fcntl
import glob
import os
import re
from pathlib import Path
import selectors
import shutil
import signal
import socket
import struct
import subprocess
import sys
import time
from dataclasses import dataclass

QUERYCAP = 0x80685600
ENUM_FMT = 0xC0405602
CAP_CAPTURE = 0x00000001
CAP_DEVICE_CAPS = 0x80000000
LABEL = "UmbraCam IR Camera"
LAUNCHER = Path(__file__).resolve().parent.parent / "UmbraCam.sh"


@dataclass(frozen=True)
class Device:
    path: str
    name: str
    driver: str
    caps: int
    formats: tuple

    @property
    def loopback(self):
        return "loopback" in self.driver

    @property
    def infrared(self):
        return (not self.loopback and bool(self.caps & CAP_CAPTURE)
                and "GREY" in self.formats)


def decode(value):
    return value.split(b"\0", 1)[0].decode("utf-8", errors="replace")


def inspect_device(path):
    with open(path, "rb", buffering=0) as device:
        cap = bytearray(104)
        fcntl.ioctl(device, QUERYCAP, cap)
        driver, name, _, _, caps, device_caps = struct.unpack_from("=16s32s32sIII", cap)
        if caps & CAP_DEVICE_CAPS:
            caps = device_caps
        formats = []
        if caps & CAP_CAPTURE:
            for index in range(256):
                desc = bytearray(64)
                struct.pack_into("=II", desc, 0, index, 1)
                try:
                    fcntl.ioctl(device, ENUM_FMT, desc)
                except OSError as exc:
                    if exc.errno == errno.EINVAL:
                        break
                    raise
                formats.append(desc[44:48].decode("ascii", errors="replace"))
        return Device(str(Path(path).resolve()), decode(name), decode(driver), caps, tuple(formats))


def devices():
    result = []
    for path in sorted(glob.glob("/dev/video*")):
        try:
            result.append(inspect_device(path))
        except OSError as exc:
            print(f"Cannot inspect {path}: {exc.strerror}", file=sys.stderr)
    return result


def choose_source(found, requested=None):
    if requested:
        device = inspect_device(requested)
        if not device.infrared:
            raise ValueError(f"{requested} is not a grayscale capture device")
        return device
    candidates = [device for device in found if device.infrared]
    if not candidates:
        raise ValueError("No GREY infrared capture device found. Run './UmbraCam.sh devices'.")
    if len(candidates) != 1:
        raise ValueError("Multiple grayscale cameras found; select one with --source /dev/videoN.")
    return candidates[0]


def choose_output(found, requested=None):
    if requested:
        device = inspect_device(requested)
        if not device.loopback:
            raise ValueError(f"Refusing to write to physical camera {requested}")
        if device.name != LABEL:
            raise ValueError(f"{requested} is not a dedicated {LABEL} device")
        return device
    candidates = [device for device in found if device.loopback and device.name == LABEL]
    if len(candidates) != 1:
        raise ValueError("Dedicated virtual camera unavailable. Run './UmbraCam.sh setup'.")
    return candidates[0]


def require(program):
    if not shutil.which(program):
        raise ValueError(f"Missing {program}. See README.md for Arch installation commands.")


def capture_command(source, args):
    return ["ffmpeg", "-hide_banner", "-loglevel", "warning", "-nostdin",
            "-f", "v4l2", "-input_format", "gray", "-video_size", args.size,
            "-framerate", str(args.fps), "-i", source.path, "-an"]


def publish_command(source, output, args):
    if source.path == output.path:
        raise ValueError("Capture and output devices must differ")
    return capture_command(source, args) + [
        "-vf", "format=yuyv422",
        "-pix_fmt", "yuyv422", "-c:v", "rawvideo", "-threads", "1",
        "-progress", "pipe:1", "-stats_period", "0.25", "-f", "v4l2", output.path]


def notify(message):
    address = os.environ.get("NOTIFY_SOCKET")
    if address:
        if address.startswith("@"):
            address = "\0" + address[1:]
        with socket.socket(socket.AF_UNIX, socket.SOCK_DGRAM) as channel:
            channel.connect(address)
            channel.sendall(message.encode())


def stop_child(child):
    if child.poll() is None:
        child.terminate()
        try:
            child.wait(timeout=3)
        except subprocess.TimeoutExpired:
            child.kill()
            child.wait()


def runtime_directory():
    base = Path(os.environ.get("XDG_RUNTIME_DIR", f"/run/user/{os.getuid()}"))
    if not base.is_dir() or base.stat().st_uid != os.getuid():
        raise ValueError("A user runtime directory is required. Run in your desktop login session.")
    directory = base / "umbracam"
    directory.mkdir(mode=0o700, exist_ok=True)
    if directory.is_symlink() or directory.stat().st_uid != os.getuid():
        raise ValueError("Unsafe runtime directory")
    return directory


def publish(args):
    require("ffmpeg")
    if os.geteuid() == 0:
        raise ValueError("Run capture as your desktop user, without sudo.")
    found = devices()
    source = choose_source(found, args.source)
    output = choose_output(found, args.output)
    lock_fd = os.open(runtime_directory() / "publisher.lock", os.O_CREAT | os.O_RDWR | os.O_NOFOLLOW, 0o600)
    with os.fdopen(lock_fd, "w") as lock:
        try:
            fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            raise ValueError("UmbraCam is already capturing in this session") from None
        print(f"{source.path} → {output.path} ({args.size}, {args.fps} fps)", flush=True)
        child = subprocess.Popen(publish_command(source, output, args), stdout=subprocess.PIPE)
        stopping = False

        def stop(_signum, _frame):
            nonlocal stopping
            stopping = True

        old_handlers = {sig: signal.signal(sig, stop) for sig in (signal.SIGINT, signal.SIGTERM)}
        ready = False
        last_frame = time.monotonic()
        last_count = 0
        pending = b""
        try:
            with selectors.DefaultSelector() as selector:
                selector.register(child.stdout, selectors.EVENT_READ)
                while not stopping:
                    if child.poll() is not None:
                        raise ValueError(f"Capture exited ({child.returncode}); check device permissions and camera use.")
                    for key, _ in selector.select(timeout=0.5):
                        chunk = os.read(key.fd, 8192)
                        if not chunk:
                            continue
                        pending += chunk
                        while b"\n" in pending:
                            line, pending = pending.split(b"\n", 1)
                            if line.startswith(b"frame="):
                                count = int(line.split(b"=", 1)[1])
                                if count > last_count:
                                    last_frame = time.monotonic()
                                    last_count = count
                                    if not ready:
                                        notify("READY=1\nSTATUS=Publishing infrared frames")
                                        print("Publishing infrared frames. Select UmbraCam IR Camera in your camera app.", flush=True)
                                        ready = True
                    if time.monotonic() - last_frame > 8:
                        raise ValueError("No new frames for eight seconds; stopping capture.")
        finally:
            stop_child(child)
            child.stdout.close()
            for sig, handler in old_handlers.items():
                signal.signal(sig, handler)
            notify("STOPPING=1")
    return 0


def probe(args):
    require("ffmpeg")
    source = choose_source(devices(), args.source)
    command = capture_command(source, args) + ["-frames:v", "15", "-vf", "scale=1:1",
                                               "-pix_fmt", "gray", "-f", "rawvideo", "pipe:1"]
    try:
        result = subprocess.run(command, stdout=subprocess.PIPE, timeout=10, check=True)
    except subprocess.TimeoutExpired as exc:
        raise ValueError("IR capture timed out; close other camera apps and check the shutter.") from exc
    samples = result.stdout
    if len(samples) != 15:
        raise ValueError(f"Expected 15 frames, received {len(samples)}")
    print(f"PASS: {source.path}: 15 frames at {args.size}; mean luminance {sum(samples) / len(samples):.1f}/255.")
    if max(samples) <= 2:
        print("Frames are nearly black. Check the shutter and lighting; IR emitter activation is hardware-specific.")
    print("No images were saved.")
    return 0


def service(args):
    require("systemctl")
    if args.action == "disable":
        return subprocess.run(["systemctl", "--user", "stop", "umbracam.service"]).returncode
    if args.action == "status":
        result = subprocess.run(["systemctl", "--user", "show", "umbracam.service",
                                 "--property=ActiveState", "--property=SubState", "--property=Result"])
        print("Status does not open the physical camera. Logs: journalctl --user -u umbracam.service")
        return result.returncode
    require("systemd-run")
    command = ["systemd-run", "--user", "--unit=umbracam", "--collect",
               "--property=Type=notify", "--property=TimeoutStartSec=15",
               "--property=TimeoutStopSec=8", "--property=KillMode=control-group",
               str(LAUNCHER), "run", "--size", args.size, "--fps", str(args.fps)]
    for flag in ("source", "output"):
        value = getattr(args, flag)
        if value:
            command.extend([f"--{flag}", value])
    result = subprocess.run(command)
    if result.returncode == 0:
        print("UmbraCam enabled. Stop after your call with './UmbraCam.sh disable'.")
    else:
        print("See: journalctl --user -u umbracam.service -n 30 --no-pager", file=sys.stderr)
    return result.returncode


def setup(_args):
    result = subprocess.run(["modinfo", "v4l2loopback"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    if result.returncode:
        raise ValueError("Install v4l2loopback-dkms and headers matching your running kernel; see README.md.")
    if any(d.loopback and d.name == LABEL for d in devices()):
        print("Dedicated UmbraCam virtual camera is ready.")
        return 0
    if Path("/sys/module/v4l2loopback").exists():
        raise ValueError("v4l2loopback is already used by another app. See README.md to add a device without unloading it.")
    number = next((n for n in range(10, 64) if not Path(f"/dev/video{n}").exists()), None)
    if number is None:
        raise ValueError("No unused virtual camera number between 10 and 63")
    require("pkexec")
    modprobe = shutil.which("modprobe")
    if not modprobe:
        raise ValueError("modprobe is missing; install kmod")
    print("Authenticate in the system dialog to load the virtual camera. Capture remains off.", flush=True)
    result = subprocess.run(["pkexec", modprobe, "v4l2loopback", "devices=1", f"video_nr={number}",
                             f"card_label={LABEL}", "exclusive_caps=1"])
    if result.returncode:
        raise ValueError("Administrator authentication or module loading failed. Run setup again to retry.")
    choose_output(devices())
    print("Virtual camera ready until reboot. Enable it when needed.")
    return 0


def doctor(_args):
    failed = False
    for program in ("python3", "ffmpeg", "ffplay", "modinfo", "modprobe", "systemctl", "systemd-run", "pkexec"):
        available = shutil.which(program)
        print(f"{program}: {available or 'MISSING'}")
        failed |= available is None
    print(f"Session: {os.environ.get('XDG_SESSION_TYPE', 'unknown')}; kernel: {os.uname().release}")
    for device in devices():
        kind = "virtual" if device.loopback else "grayscale candidate" if device.infrared else "other/metadata"
        print(f"{device.path}: {device.name} [{kind}] {', '.join(device.formats)}")
    return int(failed)


def parse_args(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("devices", "doctor", "probe", "preview", "run", "enable", "disable", "status", "setup"))
    parser.add_argument("--source", help="explicit GREY capture device (including /dev/v4l/by-id paths)")
    parser.add_argument("--output", help="dedicated UmbraCam loopback device")
    parser.add_argument("--size", default="640x400", help="capture size supported by the sensor (default: 640x400)")
    parser.add_argument("--fps", type=int, default=15, choices=range(1, 121), metavar="1..120")
    args = parser.parse_args(argv)
    if not re.fullmatch(r"[1-9][0-9]{0,3}x[1-9][0-9]{0,3}", args.size):
        parser.error("--size must be WIDTHxHEIGHT, with dimensions from 1 to 9999")
    return args


def main(argv=None):
    args = parse_args(argv)
    try:
        if args.action in ("enable", "disable", "status"):
            return service(args)
        if args.action == "run":
            return publish(args)
        if args.action == "probe":
            return probe(args)
        if args.action == "setup":
            return setup(args)
        if args.action == "doctor":
            return doctor(args)
        if args.action == "devices":
            for device in devices():
                kind = "virtual" if device.loopback else "grayscale candidate" if device.infrared else "other/metadata"
                print(f"{device.path}: {device.name} [{kind}] {', '.join(device.formats)}")
            return 0
        require("ffplay")
        source = choose_source(devices(), args.source)
        return subprocess.run(["ffplay", "-hide_banner", "-loglevel", "warning", "-f", "v4l2",
                               "-input_format", "gray", "-video_size", args.size, "-framerate", str(args.fps),
                               "-i", source.path, "-window_title", "UmbraCam IR Preview"]).returncode
    except (ValueError, OSError, subprocess.SubprocessError) as exc:
        print(f"UmbraCam: {exc}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())

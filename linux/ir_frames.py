"""Bounded frame holding for sensors alternating bright and ambient IR exposures."""

import subprocess
import threading
import time


class IrFramePolicy:
    """Repeat a recent bright exposure during a short, strongly darker phase."""

    def __init__(self, hold_seconds=1.0):
        self.hold_seconds = hold_seconds
        self.previous = None
        self.previous_mean = 0.0
        self.previous_time = 0.0
        self.held = 0
        self.total = 0

    def apply(self, frame, now):
        if not frame:
            raise ValueError("An infrared frame must not be empty")
        sample = frame[::16]
        mean = sum(sample) / len(sample)
        self.total += 1
        darker_phase = (self.previous is not None
                        and mean < self.previous_mean * 0.35
                        and self.previous_mean - mean >= 2.0)
        if darker_phase and now - self.previous_time < self.hold_seconds:
            self.held += 1
            return self.previous
        # Sustained darkness must replace the old scene, including a closed shutter.
        self.previous = frame
        self.previous_mean = mean
        self.previous_time = now
        return frame


def stop_process(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()


def write_frame(stream, frame):
    remaining = memoryview(frame)
    while remaining:
        written = stream.write(remaining)
        if not written:
            raise BrokenPipeError("Publisher stopped accepting infrared frames")
        remaining = remaining[written:]


class FrameRelay:
    """Read complete GREY frames and feed a publisher without unbounded buffering."""

    def __init__(self, command, destination, frame_size):
        self.destination = destination
        self.frame_size = frame_size
        self.policy = IrFramePolicy()
        self.error = None
        self.capture = subprocess.Popen(command, stdout=subprocess.PIPE)
        self.worker = threading.Thread(target=self._run, name="ir-frames", daemon=True)
        self.worker.start()

    def _run(self):
        try:
            while True:
                frame = self.capture.stdout.read(self.frame_size)
                if not frame:
                    break
                if len(frame) != self.frame_size:
                    raise ValueError("Infrared capture ended with an incomplete frame")
                write_frame(self.destination, self.policy.apply(frame, time.monotonic()))
        except (OSError, ValueError) as exc:
            self.error = str(exc)
        finally:
            try:
                self.destination.close()
            except OSError:
                pass

    def stop_capture(self):
        stop_process(self.capture)

    def close(self):
        self.worker.join(timeout=2)
        self.capture.stdout.close()
        print(f"IR flicker reduction: held {self.policy.held} of {self.policy.total} exposures.", flush=True)

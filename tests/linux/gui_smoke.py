"""Exercise GTK construction and asynchronous status updates without capture."""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "linux"))
from gui import Application, GLib

application = Application()
failures = []


def check():
    try:
        assert application.window.get_visible(), "Window must be visible"
        assert application.source.get_active_id() == "auto"
        assert not application.busy
        assert application.command("enable")[-4:] == ["--size", "640x400", "--fps", "15"]
    except Exception as exc:
        failures.append(str(exc))
    application.quit()
    return False


GLib.timeout_add(1500, check)
application.run(["gui-smoke"])
if failures:
    raise SystemExit("; ".join(failures))
print("GTK launch and asynchronous status: PASS (no capture started)")

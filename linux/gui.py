"""Desktop controls for the UmbraCam Linux publisher."""

import sys

try:
    import gi
    gi.require_version("Gtk", "3.0")
    from gi.repository import Gio, GLib, Gtk
except (ImportError, ValueError) as exc:
    print("Desktop menu needs python-gobject and gtk3. Install them with pacman, or use the CLI.", file=sys.stderr)
    raise SystemExit(1) from exc

import umbracam


class Application(Gtk.Application):
    def __init__(self):
        super().__init__(application_id="org.umbracam.Linux")
        self.window = None
        self.busy = False
        self.status_pending = False
        self.active = False
        self.previewing = False

    def do_activate(self):
        if self.window:
            self.window.present()
            return
        self.window = Gtk.ApplicationWindow(application=self, title="UmbraCam")
        self.window.set_default_size(540, 420)
        self.window.set_border_width(24)
        self.window.set_icon_name("camera-web")
        box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=14)
        self.window.add(box)

        title = Gtk.Label(xalign=0)
        title.set_markup('<span size="xx-large" weight="bold">Infrared webcam</span>')
        box.pack_start(title, False, False, 0)
        subtitle = Gtk.Label(label="Use your IR sensor in your camera and calling apps.", xalign=0)
        box.pack_start(subtitle, False, False, 0)
        self.state = Gtk.Label(label="Checking camera status…", xalign=0)
        box.pack_start(self.state, False, False, 0)

        self.source = Gtk.ComboBoxText()
        self.source.append("auto", "Automatically select infrared sensor")
        try:
            for device in umbracam.devices():
                if device.infrared:
                    self.source.append(device.path, f"{device.name} — {device.path}")
        except OSError:
            pass
        self.source.set_active(0)
        box.pack_start(self.source, False, False, 0)

        profile = Gtk.Box(spacing=10)
        profile.pack_start(Gtk.Label(label="Resolution"), False, False, 0)
        self.size = Gtk.Entry(text="640x400", width_chars=10)
        profile.pack_start(self.size, True, True, 0)
        profile.pack_start(Gtk.Label(label="FPS"), False, False, 0)
        self.fps = Gtk.SpinButton.new_with_range(1, 120, 1)
        self.fps.set_value(15)
        profile.pack_start(self.fps, False, False, 0)
        box.pack_start(profile, False, False, 0)

        actions = Gtk.Box(spacing=10)
        self.enable = Gtk.Button(label="Enable camera")
        self.enable.get_style_context().add_class("suggested-action")
        self.enable.connect("clicked", lambda _button: self.action("enable"))
        actions.pack_start(self.enable, True, True, 0)
        self.disable = Gtk.Button(label="Disable")
        self.disable.connect("clicked", lambda _button: self.action("disable"))
        actions.pack_start(self.disable, True, True, 0)
        self.preview = Gtk.Button(label="Preview IR")
        self.preview.connect("clicked", self.open_preview)
        actions.pack_start(self.preview, True, True, 0)
        box.pack_start(actions, False, False, 0)

        tools = Gtk.Box(spacing=10)
        self.setup = Gtk.Button(label="Set up virtual camera")
        self.setup.connect("clicked", lambda _button: self.action("setup"))
        tools.pack_start(self.setup, False, False, 0)
        refresh = Gtk.Button(label="Refresh status")
        refresh.connect("clicked", lambda _button: self.refresh())
        tools.pack_start(refresh, False, False, 0)
        box.pack_start(tools, False, False, 0)

        hint = Gtk.Label(xalign=0, wrap=True)
        hint.set_text("Select “UmbraCam IR Camera” in your calling app.\nClosing this window leaves an enabled camera running.")
        box.pack_start(hint, False, False, 0)
        self.message = Gtk.Label(xalign=0, wrap=True, selectable=True)
        box.pack_start(self.message, False, False, 0)

        self.details = Gtk.TextView(editable=False, cursor_visible=False, monospace=True)
        self.details.set_wrap_mode(Gtk.WrapMode.WORD_CHAR)
        scroll = Gtk.ScrolledWindow()
        scroll.set_min_content_height(100)
        scroll.add(self.details)
        expander = Gtk.Expander(label="Details")
        expander.add(scroll)
        box.pack_start(expander, True, True, 0)
        self.window.show_all()
        self.refresh()
        GLib.timeout_add_seconds(4, self.refresh)

    def command(self, action):
        command = [str(umbracam.LAUNCHER), action]
        if action in ("enable", "preview"):
            command.extend(["--size", self.size.get_text(), "--fps", str(self.fps.get_value_as_int())])
            source = self.source.get_active_id()
            if source and source != "auto":
                command.extend(["--source", source])
        return command

    def action(self, action):
        if self.busy:
            return
        self.busy = True
        self.update_buttons()
        self.message.set_text("Authenticate in the system dialog if requested." if action == "setup" else "Working…")
        try:
            process = Gio.Subprocess.new(self.command(action), Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_MERGE)
            process.communicate_utf8_async(None, None, self.action_finished, action)
        except GLib.Error as exc:
            self.busy = False
            self.message.set_text(str(exc))
            self.update_buttons()

    def action_finished(self, process, result, action):
        try:
            _, output, _ = process.communicate_utf8_finish(result)
            self.details.get_buffer().set_text(output or "")
            messages = {"enable": "Camera enabled.", "disable": "Camera disabled.",
                        "setup": "Virtual camera is ready. Enable it when needed."}
            self.message.set_text(messages[action] if process.get_successful() else "Action failed. Open Details for the error.")
        except GLib.Error as exc:
            self.message.set_text(str(exc))
        self.busy = False
        self.refresh()
        self.update_buttons()

    def open_preview(self, _button):
        if self.active:
            self.message.set_text("Disable the camera before opening the physical IR preview.")
            return
        self.previewing = True
        self.update_buttons()
        try:
            process = Gio.Subprocess.new(self.command("preview"), Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_MERGE)
            process.communicate_utf8_async(None, None, self.preview_finished)
        except GLib.Error as exc:
            self.previewing = False
            self.update_buttons()
            self.message.set_text(str(exc))

    def preview_finished(self, process, result):
        self.previewing = False
        self.update_buttons()
        try:
            _, output, _ = process.communicate_utf8_finish(result)
            if not process.get_successful():
                self.details.get_buffer().set_text(output or "")
                self.message.set_text("Preview closed with an error. Open Details.")
        except GLib.Error as exc:
            self.message.set_text(str(exc))

    def refresh(self):
        if not self.status_pending:
            self.status_pending = True
            try:
                process = Gio.Subprocess.new(["systemctl", "--user", "show", "umbracam.service",
                                              "--property=ActiveState", "--value"],
                                             Gio.SubprocessFlags.STDOUT_PIPE | Gio.SubprocessFlags.STDERR_PIPE)
                process.communicate_utf8_async(None, None, self.status_finished)
            except GLib.Error:
                self.state.set_text("Background controls need a systemd user session.")
                self.status_pending = False
        return True

    def status_finished(self, process, result):
        try:
            _, output, _ = process.communicate_utf8_finish(result)
            self.active = output.strip() == "active"
            if process.get_successful():
                self.state.set_text("● Camera on" if self.active else "○ Background camera off")
            else:
                self.state.set_text("Background controls need a systemd user session.")
        except GLib.Error:
            self.state.set_text("Unable to read camera status.")
        self.status_pending = False
        self.update_buttons()

    def update_buttons(self):
        self.enable.set_sensitive(not self.busy and not self.active and not self.previewing)
        self.disable.set_sensitive(not self.busy and self.active)
        self.preview.set_sensitive(not self.busy and not self.active and not self.previewing)
        self.setup.set_sensitive(not self.busy and not self.active)


def main():
    return Application().run([sys.argv[0]])


if __name__ == "__main__":
    sys.exit(main())

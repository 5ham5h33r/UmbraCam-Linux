#!/usr/bin/env python3
"""Install desktop shortcuts for this checkout without starting capture."""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys

LAUNCHER = Path(__file__).resolve().parent.parent / "UmbraCam.sh"
DESKTOP_NAME = "org.umbracam.Linux.desktop"
MARKER = "# UmbraCam desktop entry\n"


def desktop_entry(launcher):
    path = str(launcher)
    if any(character in path for character in ("\n", "\r", "\0")):
        raise ValueError("Launcher path contains an unsupported control character")
    # Desktop entries unescape backslashes before parsing quoted Exec arguments.
    path = path.replace("\\", "\\\\\\\\").replace('"', '\\\\"').replace("`", "\\\\`").replace("$", "\\\\$").replace("%", "%%")
    return MARKER + f'''[Desktop Entry]
Type=Application
Name=UmbraCam
GenericName=Infrared Webcam
Comment=Enable and manage your infrared webcam
Exec="{path}" menu
Icon=camera-web
Terminal=false
StartupNotify=true
Categories=AudioVideo;Video;
'''


def install(bin_dir, data_dir, launcher=LAUNCHER):
    command = bin_dir / "umbracam"
    desktop = data_dir / "applications" / DESKTOP_NAME
    if command.exists() or command.is_symlink():
        if not command.is_symlink() or command.resolve() != launcher.resolve():
            raise ValueError(f"Refusing to replace existing {command}")
    if desktop.exists() or desktop.is_symlink():
        if desktop.is_symlink() or not desktop.read_text().startswith(MARKER):
            raise ValueError(f"Refusing to replace existing {desktop}")
    entry = desktop_entry(launcher)
    bin_dir.mkdir(parents=True, exist_ok=True)
    desktop.parent.mkdir(parents=True, exist_ok=True)
    if not command.is_symlink():
        command.symlink_to(launcher)
    desktop.write_text(entry)
    return command, desktop


def uninstall(bin_dir, data_dir, launcher=LAUNCHER):
    command = bin_dir / "umbracam"
    desktop = data_dir / "applications" / DESKTOP_NAME
    if command.is_symlink() and command.resolve() == launcher.resolve():
        command.unlink()
    if desktop.is_file() and not desktop.is_symlink() and desktop.read_text().startswith(MARKER):
        desktop.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--uninstall", action="store_true", help="remove only this checkout's shortcuts")
    args = parser.parse_args()
    if os.geteuid() == 0:
        parser.error("run as your desktop user, without sudo")
    bin_dir = Path.home() / ".local" / "bin"
    data_dir = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local" / "share"))
    try:
        if args.uninstall:
            uninstall(bin_dir, data_dir)
            print("Desktop shortcuts removed. Camera state was not changed.")
        else:
            command, desktop = install(bin_dir, data_dir)
            print(f"Installed {command}\nInstalled {desktop}")
            print("Open UmbraCam from your application menu. Capture remains off until enabled.")
        if shutil.which("update-desktop-database"):
            subprocess.run(["update-desktop-database", str(data_dir / "applications")], check=False)
        return 0
    except (OSError, ValueError) as exc:
        print(f"UmbraCam: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())

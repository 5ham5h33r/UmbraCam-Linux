# UmbraCam-Linux

Use a laptop's infrared sensor as **UmbraCam IR Camera** in webcam applications
on Linux. The bridge captures a V4L2 grayscale stream and publishes a YUYV
virtual camera through `v4l2loopback`. It runs locally, without OBS, Python
packages from pip, or recorded video.

Based on [UmbraCam](https://github.com/5ham5h33r/UmbraCam), with the original
history and Windows implementation retained. GitHub does not create a second
same-owner fork, so this is an independent derivative repository.
The original [Windows instructions](docs/windows.md) remain available.

## Compatibility

Targets Arch Linux and Arch derivatives, including CachyOS, EndeavourOS and
Manjaro. The application checks Linux device capabilities; it has no dependency
on a particular distribution, kernel name, desktop, X11, or Wayland.

Requirements:

- Python 3.11 or newer, FFmpeg with V4L2 support, and `kmod`.
- A camera exposing **GREY / 8-bit grayscale** through V4L2. Windows Hello
  branding alone does not guarantee Linux sensor support.
- `v4l2loopback` built for the **running** kernel.
- A systemd desktop user session for background enable/disable; foreground
  `run`, `probe` and `preview` also work without systemd.
- `polkit` and a desktop authentication agent for graphical module setup.
  A manual `sudo modprobe` alternative is documented below.

The default capture profile is 640×400 at 15 fps. Use `--size WIDTHxHEIGHT`
and `--fps N` if your sensor exposes a different profile. Device numbers are
rediscovered on each launch; `/dev/video2` is not hardcoded.

Validated on CachyOS with a Shinetech `3277:0060` IR sensor. Arch container CI
checks the current Arch userspace. Other distributions and camera models need
hardware validation; see [the test report](docs/linux-validation.md).

## Install dependencies

On Arch, CachyOS and EndeavourOS:

```sh
sudo pacman -Syu --needed python ffmpeg kmod polkit
modinfo v4l2loopback
```

If `modinfo` succeeds, your kernel already provides the module. Otherwise,
install the module sources and **matching headers**:

```sh
# Example for the stock Arch linux kernel:
sudo pacman -S --needed v4l2loopback-dkms linux-headers
```

Use `linux-lts-headers`, `linux-zen-headers`, or `linux-cachyos-headers` when
appropriate. Manjaro uses kernel-versioned headers, such as `linuxXYZ-headers`;
check `uname -r` and your distribution's kernel manager instead of installing
stock Arch headers. Reboot first if a kernel update left your running kernel
without matching module files. Do not mix another distribution's repositories.

## Quick start

```sh
git clone https://github.com/5ham5h33r/UmbraCam-Linux.git
cd UmbraCam-Linux
./UmbraCam.sh doctor
./UmbraCam.sh setup    # administrator prompt; creates virtual device only
./UmbraCam.sh probe    # reads 15 IR frames, prints statistics, saves no video
./UmbraCam.sh enable   # returns success after frames reach the virtual camera
```

Select **UmbraCam IR Camera** in your camera application's device selector.
Start the publisher before opening the selector: with `exclusive_caps=1`,
Chromium sees the loopback as a camera only while a producer is attached.

After your call:

```sh
./UmbraCam.sh disable
./UmbraCam.sh status
```

Capture does not start at login or boot. Closing your terminal after `enable`
leaves the background publisher running. Always disable after use. `status`
reads service state without opening the physical camera. Stream failures or
eight seconds without new frames stop the publisher; logs explain why:

```sh
journalctl --user -u umbracam.service -n 40 --no-pager
```

Other commands:

```sh
./UmbraCam.sh devices
./UmbraCam.sh preview                  # physical IR preview; close to stop
./UmbraCam.sh run                      # foreground publishing; Ctrl+C to stop
./UmbraCam.sh enable --source /dev/v4l/by-id/YOUR-IR-DEVICE --size 640x400 --fps 15
```

Close preview/probe before enabling: a physical sensor may allow only one
capture client. If multiple grayscale devices exist, select one explicitly.
GREY identifies a grayscale candidate, not proof that a sensor is infrared.
The publisher only writes to a loopback named **UmbraCam IR Camera**, and refuses
physical cameras or another application's loopback.

## Virtual camera setup

`setup` loads an existing module through a normal administrator authentication
dialog. It picks a free device number starting at 10 and does not change files
under `/etc`. Run it again after reboot. If your desktop lacks an authentication
agent, run this in a terminal, using a free device number:

```sh
sudo modprobe v4l2loopback devices=1 video_nr=10 \
  card_label='UmbraCam IR Camera' exclusive_caps=1
```

If OBS or another app has already loaded `v4l2loopback`, UmbraCam leaves it
alone. With your distribution's `v4l2loopback-utils` installed, add a dedicated
camera without unloading existing devices:

```sh
sudo v4l2loopback-ctl add -n 'UmbraCam IR Camera' -x 1 /dev/video10
```

Consult `v4l2loopback-ctl add --help` for the installed version's options. To
remove this dynamically added device, stop UmbraCam and its clients, then use
`sudo v4l2loopback-ctl delete /dev/video10`.

For optional module loading at boot, configure your distribution's modules-load
and modprobe configuration with the same label and `exclusive_caps=1`.
This creates a device at boot; **it must not start capture automatically**.
Merge options with any existing loopback configuration to preserve OBS devices.

## Limitations and troubleshooting

- A successful probe verifies frame delivery, not image quality. Nearly black
  frames can mean a closed shutter, low ambient IR, or an inactive IR emitter.
  UmbraCam does not send undocumented USB emitter commands or bypass shutters.
- The Linux bridge passes through all exposures. Windows' illuminated-frame
  filtering depends on metadata that this backend does not consume yet.
- Verify camera permissions through your desktop login session. Do not run the
  publisher as root or make camera nodes world-writable.
- Sandboxed browsers and Flatpaks may need camera/portal permission. Actual
  Meet, Teams and Zoom calls require separate validation.
- Without a systemd user session (for example, some Artix installations), use
  `run` and stop it with Ctrl+C; `enable`, `disable` and `status` need systemd.
- Secure Boot may require signing an out-of-tree DKMS module according to your
  distribution's instructions.

## Development

```sh
python3 -m unittest discover -s tests/linux -v
python3 -m compileall -q linux
sh -n UmbraCam.sh
```

See [CONTRIBUTING.md](CONTRIBUTING.md) for the feature → dev → main workflow.
Upstream notices and third-party license texts remain in
[NOTICE](NOTICE) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Implementation references: [FFmpeg's V4L2 device documentation](https://ffmpeg.org/ffmpeg-devices.html#video4linux2_002c-v4l2)
and [v4l2loopback configuration](https://github.com/v4l2loopback/v4l2loopback#options).

# Linux validation

## Scope

Hardware checks performed on 2026-11-04:

- CachyOS, x86_64, Linux `7.3.0-5-cachyos`, Wayland desktop.
- Shinetech USB `3277:0060` RGB/IR camera.
- IR: V4L2 `uvcvideo`, GREY 640×400 at 15 fps.
- FFmpeg 9.0.2, Python 3.14.8, kernel-provided v4l2loopback 0.15.4.

No images or recordings were saved during validation.

| Check | Result |
| --- | --- |
| Discover RGB, IR, metadata and dedicated virtual nodes | Pass; IR selected by capture capability and GREY format |
| Physical IR probe | Pass; 15 frames received |
| Background start | Pass; systemd Type=notify readiness after published frames |
| Service state while publishing | Active/running, result=success |
| Independent virtual-camera reader | Pass; FFmpeg received 20 frames |
| Graceful disable | Pass; inactive/dead, result=success |
| Default grayscale image brightness | Probe about 4/255 initially; full 15-frame sample 14.57/255, range 0–255, standard deviation 32.34; scene-dependent |
| Unit tests | 22 pass locally on Python 3.14 |
| Desktop GTK startup and async status | Pass on Wayland; no capture started |
| Desktop Enable / Disable buttons | Pass; 20 independent virtual-camera frames received between actions |
| Desktop shortcut format | Pass with desktop-file-validate, including paths with spaces and special characters |
| Repeated enable | Rejects duplicate service without disturbing the running publisher |
| Other Arch derivatives on physical hardware | Not tested |
| Browser camera selector / WebRTC | Not tested |
| Meet, Teams or Zoom call | Not tested |
| IR emitter activation and dark-room image quality | Not tested |
| Suspend/resume, unplug/replug | Not tested |

The CI workflow exercises Python 3.11–3.14 on Ubuntu and current Arch Linux
userspace in a container. Containers validate code and dependencies, not USB
hardware or a kernel module. Consult the actual workflow results for pass/fail.

## Reproduce

```sh
./UmbraCam.sh doctor
./UmbraCam.sh setup
./UmbraCam.sh probe
./UmbraCam.sh enable
./UmbraCam.sh status
# Substitute the discovered dedicated loopback path if different:
timeout 10 ffmpeg -hide_banner -loglevel error -f v4l2 \
  -i /dev/video10 -frames:v 20 -f null -
./UmbraCam.sh disable
./UmbraCam.sh status
journalctl --user -u umbracam.service -n 40 --no-pager
```

Verify capture is stopped after testing. Never substitute a physical camera
path for the publisher output. Brightness results depend on the scene, shutter
and IR illumination; a frame count alone is not an image-quality check.

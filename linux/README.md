# LLCV for Linux

> [한국어](README.ko.md)

LLCV shows video and audio from USB (UVC/V4L2) HDMI capture devices with low
latency. This is a Linux rewrite, by Meo-hong, of the Windows LLCV 2.0.5 viewer
by [seria-aa](https://github.com/seria-aa/LowLatencyCaptureViewer). The
reference device is the AVerMedia Live Gamer ULTRA S GC553Pro. The reference
desktop is Ubuntu 26.04 (GNOME, Wayland).

> **Maintained separately from the original LLCV.** The Linux port is
> developed, released and supported by Meo-hong in
> [Meo-Hong/LLCV-for-Linux](https://github.com/Meo-Hong/LLCV-for-Linux). It is
> not an official release of the original project, and the original author
> does not maintain it. Report Linux problems in
> [this repository's issues](https://github.com/Meo-Hong/LLCV-for-Linux/issues),
> not in the original repository.

| Area | Windows build | Linux build |
| --- | --- | --- |
| Video capture | DirectShow | V4L2 memory-mapped buffers, latest frame first |
| Presentation | D3D11 / DXGI | OpenGL 3.3 (EGL on Wayland, GLX on X11), or OpenGL ES 3.0 |
| Color conversion | D3D11 Video Processor | GLSL shaders, BT.601/709/2020, limited/full range |
| HDR10 | P010 + DXGI HDR10 | P010 + Wayland `wp_color_management_v1`, or tone mapping |
| MJPEG | Media Foundation | libjpeg-turbo (decoded to YUV planes) |
| Audio | WASAPI / ASIO | SDL3 audio on PipeWire |
| UI | Win32 + Direct2D | Dear ImGui 1.92 (bundled), Pretendard font |
| Settings | `%LOCALAPPDATA%` | `~/.config/llcv/settings.ini` |
| Logs | `%LOCALAPPDATA%` | `~/.local/state/llcv/logs/` |
| Screenshots | Pictures | `$XDG_PICTURES_DIR/LLCV/` |
| Updates | In-app update check | APT repository (`.deb`) |

Not available on Linux: NVIDIA VSR, WASAPI Exclusive, ASIO, console LPCM 5.1,
and the vendor tone-mapping requests.

## Install

Download the file for your system from
[Releases](https://github.com/Meo-Hong/LLCV-for-Linux/releases/latest).

| File | Install on | Command |
| --- | --- | --- |
| `llcv_2.0.5_amd64.deb` | Ubuntu 22.04 to 26.04, Debian 12 and 13, Linux Mint, Pop!_OS, Zorin OS, elementary OS, LMDE, Kali | `sudo apt install ./llcv_2.0.5_amd64.deb` |
| `llcv_2.0.5_arm64.deb` | Raspberry Pi OS 64-bit, Debian 12 and 13 (arm64) | `sudo apt install ./llcv_2.0.5_arm64.deb` |
| `llcv_2.0.5_raspberrypi-32bit_armhf.deb` | Raspberry Pi OS 32-bit (trixie) | `sudo apt install ./llcv_2.0.5_raspberrypi-32bit_armhf.deb` |
| `llcv-2.0.5-1.x86_64.rpm` | Fedora, Nobara, RHEL, Rocky Linux, AlmaLinux 9 and 10 | `sudo dnf install ./llcv-2.0.5-1.x86_64.rpm` |
| `llcv-2.0.5-1-x86_64.pkg.tar.zst` | Arch Linux, Manjaro, EndeavourOS | `sudo pacman -U ./llcv-2.0.5-1-x86_64.pkg.tar.zst` |

- Installing a `.deb` also registers the LLCV APT repository, so later
  versions arrive through `apt upgrade` or Software Updater. For `.rpm` and
  Arch, download the new file from Releases.
- `SHA256SUMS` on the release page checks the downloads:
  `sha256sum -c SHA256SUMS --ignore-missing`.
- The notice "Download is performed unsandboxed as root" when installing from
  your home folder is harmless.
- Remove with `sudo apt remove llcv` (`sudo apt purge llcv` also removes the
  repository entry), `sudo dnf remove llcv` or `sudo pacman -R llcv`.

## Supported systems

| Distribution | File | HDR output | Tested |
| --- | --- | --- | --- |
| Ubuntu 26.04 | amd64 `.deb` | yes (GNOME 48 or newer) | **runs on real hardware**, APT updates work |
| Ubuntu 24.04, Linux Mint 22, Pop!_OS 24.04, Zorin OS 18 | amd64 `.deb` | no (GNOME 46) | build, install check |
| Ubuntu 22.04, Linux Mint 21, Pop!_OS 22.04, Zorin OS 17, elementary OS 7 | amd64 `.deb` | no (GNOME 42) | build, install check |
| Debian 13, Kali | amd64 `.deb` | yes (GNOME 48 / KDE Plasma 6) | install check |
| Debian 12, LMDE 6 | amd64 `.deb` | no (GNOME 43) | install check |
| Raspberry Pi OS 64-bit (bookworm, trixie) | arm64 `.deb` | no | build |
| Raspberry Pi OS 32-bit (trixie) | armhf `.deb` | no | build |
| Fedora 43 or newer, Nobara | `.rpm` | yes (GNOME 48 or newer / KDE Plasma 6) | install check (Fedora 44) |
| RHEL, Rocky Linux, AlmaLinux 9 and 10 | `.rpm` | no (GNOME 40 / 47) | build, install check |
| Arch Linux, Manjaro, EndeavourOS | `.pkg.tar.zst` | yes (GNOME 48 or newer / KDE Plasma 6) | build |

"Install check" means the package manager accepted the package and its
dependencies; the app itself was not run on that system. HDR output needs a
Wayland session. Raspberry Pi OS 32-bit needs a Raspberry Pi 2 or newer; Pi
Zero and Pi 1 are not supported, and Raspberry Pi OS bookworm 32-bit is not
supported.

| GPU | Driver | Presentation | HDR10 passthrough (Wayland) | Tested |
| --- | --- | --- | --- | --- |
| NVIDIA | proprietary | OpenGL 3.3 | when the driver offers a 10-bit EGL buffer | **real hardware (GeForce RTX 2070)** |
| AMD | Mesa | OpenGL 3.3 | yes | not yet |
| Intel | Mesa | OpenGL 3.3 | yes | not yet |
| Raspberry Pi 4/5 | Mesa v3d | OpenGL ES 3.0 | no (the Pi desktop has no color management) | not yet |

When desktop OpenGL 3.3 is not available, LLCV switches to OpenGL ES 3.0
automatically. On a Raspberry Pi, MJPEG is decoded by the CPU, so prefer NV12
or YUY2 at 1080p; 4K MJPEG is too heavy for a Pi 4.

The HDR path does not depend on the driver's own color-management support.
LLCV tags its window with `wp_color_management_v1` itself, so the compositor
does the HDR work. The driver only has to provide a 10-bit buffer. Without
one, Auto mode tone maps to SDR.

NVIDIA on Wayland needs the EGL Wayland platform library. Debian and Ubuntu
ship it in `libnvidia-egl-wayland1`. Arch ships it in `egl-wayland`, which
`nvidia-utils` already depends on. Fedora and EL ship it in `egl-wayland`
(RPM Fusion or the NVIDIA repository installs it with the driver).

When HDR is not available, the app says so and gives the reason. The reason
appears in Settings → Video → HDR, in the F1 help, in the Tab diagnostics,
and in a message when an HDR session starts. HDR signals are then shown as
SDR.

### Capture modes (GC553Pro)

| Resolution | NV12 (uncompressed) | MJPEG | P010 (HDR) |
| --- | --- | --- | --- |
| 1920 × 1080 | up to 120 fps | 240 fps | up to 60 fps |
| 2560 × 1440 | up to 60 fps | 120 / 144 fps | up to 30 fps |
| 3840 × 2160 | up to 30 fps | 60 fps | not offered |

The frame rate list in the settings comes from the device for the selected
resolution and format. P010 needs Linux 7.1 or newer (see [HDR](#hdr)).

### 40 fps capture

**Video → Capture → 40 fps capture** is off by default. When checked, LLCV
asks the capture driver for 40 fps instead of the frame-rate selection.
Unchecking it restores that selection.

- 40 fps can be requested even when the device does not list it. Support
  depends on the device, resolution and pixel format.
- On **Start**, LLCV checks that the driver actually set 40 fps. If the
  driver rejects it or sets another rate, LLCV shows an error and does not
  fall back to another rate. Uncheck the option and start again.
- It does not interpolate frames, lock a game to 40 fps, or replace VRR. A
  40 fps game can also be viewed with 120 fps capture, so the option is not
  required for it.
- Check the measured input rate under **Actual FPS** in the Tab diagnostics.

## Building from source

```sh
git clone https://github.com/Meo-Hong/LLCV-for-Linux.git
cd LLCV-for-Linux/linux
./tools/install-build-deps.sh
./tools/build-package.sh
```

The scripts detect the distribution, install the build dependencies, and
build a `.deb`, `.rpm` or Arch package. The last command prints the install
command for your system. [BUILDING.md](BUILDING.md) covers the rest:

- which package each distribution uses
- manual builds for the Debian, Fedora/RHEL and Arch families
- Docker builds for other releases and for the Raspberry Pi (ARM)
- builds without packaging, CMake options, and build troubleshooting

## HDR

1. **Capture.** HDR10 arrives as P010 (10-bit). The GC553Pro offers P010 up to
   2560 × 1440 at 30 fps, and 1080p at 60 fps. Linux 7.0 and older kernels do
   not recognize P010 in `uvcvideo` (`Unknown video format 30313050-…` in the
   kernel log). Use Linux 7.1 or newer, or a uvcvideo DKMS module with P010
   support. The settings screen warns when the device offers P010 but the
   kernel hides it.
2. **HDR displays (Wayland).** With GNOME 48 or newer, or KDE Plasma 6, and HDR
   turned on in the display settings, Auto mode passes HDR10 through
   unchanged.
3. **SDR displays and X11.** HDR10 is tone mapped to SDR with the same curve
   as the Windows build's HDR screenshots. The reference white is adjustable;
   the default is 203 nit.

Screenshots of HDR input are always saved as SDR PNGs.

If a 10-bit buffer misbehaves with a driver, start with `llcv --sdr`, or use the
"Start without HDR" launcher action. Then choose **HDR presentation → Always SDR
tone mapping**.

## Wayland and X11

LLCV uses Wayland by default. To use X11 (XWayland), do one of these:

- Select **App → Display backend → X11**. The change applies on the next launch.
- Start with `llcv --x11`.
- Use the "Start in X11 mode" action of the desktop launcher.

On Wayland the compositor places windows. Because of this, the startup monitor
setting applies only to fullscreen, and the last window position is not
restored. X11 mode restores both, but has no HDR output.

## Sleep prevention

**App → Power → Prevent sleep during playback** is on by default.

- During video playback, the display and the system stay awake.
- In audio-only mode, the display can turn off; only automatic suspend is
  blocked.
- The request is released when playback stops or the capture device
  disconnects. The settings screen does not block the screen saver or display
  power-off.
- System power settings are not changed.

SDL keeps the display on through Wayland idle-inhibit or the screen-saver
D-Bus interface. Automatic suspend is blocked through the desktop portal
(`org.freedesktop.portal.Inhibit`), so `xdg-desktop-portal` must be running
(GNOME, KDE Plasma and most desktops). Without a portal, the diagnostic log
says so.

## Menu bar

Press and release **Alt** during playback to show the menu bar. It holds the
View, Audio, Tools, Settings and Help commands, so you can use them without
remembering the function keys. Press Alt or the Exit key (default Esc) again,
or click outside the menu, to close it.

## Shortcuts

The keys below are the defaults. They can be changed under
**Settings → Guide & logs**.

| Default key | Action |
| --- | --- |
| Alt | Show or hide the menu bar |
| F1 | App information and shortcuts |
| F2 | Stop playback and open settings |
| F3 | Audio meters and volume |
| F5 | Restore the 1:1 source size |
| F11 / Alt+Enter | Fullscreen |
| F12 | PNG screenshot at the capture resolution |
| Tab | Live diagnostics |
| Esc | Leave fullscreen, or exit |
| Mouse wheel | Volume ±5% |
| Double-click | Toggle fullscreen |

### Changing shortcuts

1. Open **Settings → Guide & logs**, then click the key button beside an
   action.
2. Press a key, optionally with **Ctrl**, **Alt** or **Shift**. Click the same
   button again to cancel.
3. Choose **Start**. The new keys are saved with the other settings.

All eight shortcuts can be changed, including **Leave fullscreen / exit**
(Esc). None can be empty. Duplicate keys, modifier-only keys, Super (Windows
key) combinations and reserved combinations such as Alt+F4, Alt+Tab,
Ctrl+Alt+Delete and Ctrl+Alt+F1 to F12 cannot be assigned. **Reset defaults**
restores all eight. Alt (menu bar) and Alt+Enter (fullscreen) stay fixed.

Keys are matched by their physical position, so they keep working when a
Korean or other input method is active. The help window, menu bar, overlays
and settings show the assigned keys.

## Troubleshooting

- **No picture.** Close OBS and any other application that uses the capture
  device.
- **Only 60 fps in the list.** The device offers higher rates only at some
  resolutions. Choose 1920 × 1080 for 120 fps (NV12), or 2560 × 1440 with
  MJPEG for 120 or 144 fps. The source must send a matching signal, and VSync
  presentation is limited to the monitor's refresh rate.
- **4K 60 fps.** The GC553Pro offers 4K 60 fps only as MJPEG. Uncompressed
  NV12 is limited to 4K 30 fps, and P010 to 1440p 30 fps.
- **Washed-out colors with an HDR source.** The source is sending HDR while an
  8-bit format is captured. Select P010, use **Force HDR10**, or turn off HDR
  on the source.
- **No sound.** Check **Audio → Capture audio device**. Auto mode selects the
  audio device of the same USB device as the video.
- **Crackling audio.** Raise the PCM buffer target in 5 ms steps.
- **40 fps capture will not start.** The driver rejected 40 fps for this
  device, resolution or format, or set another rate. Read the error and the
  `40 fps request` line in the diagnostic log, then choose another resolution
  or format, or uncheck **40 fps capture**.
- **A shortcut does not work.** Click the viewer to focus LLCV, then check
  the keys under **Settings → Guide & logs**. Video-only actions do nothing in
  audio-only mode. If you forget the settings key, open settings from the
  menu bar (**Alt**) or start `llcv --settings`, then choose
  **Reset defaults**.
- **Permission denied on /dev/video\*.** Log in at the local seat, or add your
  user to the `video` group.

## Updates

The `.deb` packages register the APT repository
`https://meo-hong.github.io/LLCV-for-Linux/` (suite `stable`), signed with the
"LLCV Archive Signing Key <busimh001@gmail.com>". New versions then arrive with
the regular system updates. Each release is built by GitHub Actions from a
`linux-v*` tag, which also publishes the files on the Releases page. See
[packaging/README.md](packaging/README.md) for the release process.

## License

GPL-3.0-or-later. The original Windows LLCV is by seria-aa; the Linux port is
by Meo-hong. Dear ImGui is MIT licensed. Pretendard is licensed under the SIL
Open Font License 1.1.

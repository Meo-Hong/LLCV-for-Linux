# LLCV for Linux

> [한국어](README.ko.md)

LLCV shows video and audio from USB (UVC/V4L2) HDMI capture devices with low
latency. This directory is a Linux rewrite of the Windows LLCV 2.0.2 viewer.
The reference device is the AVerMedia Live Gamer ULTRA S GC553Pro. The
reference desktop is Ubuntu 26.04 (GNOME, Wayland).

| Area | Windows build | Linux build |
| --- | --- | --- |
| Video capture | DirectShow | V4L2 memory-mapped buffers, latest frame first |
| Presentation | D3D11 / DXGI | OpenGL 3.3 (EGL on Wayland, GLX on X11) |
| Color conversion | D3D11 Video Processor | GLSL shaders, BT.601/709/2020, limited/full range |
| HDR10 | P010 + DXGI HDR10 | P010 + Wayland `wp_color_management_v1`, or tone mapping |
| MJPEG | Media Foundation | libjpeg-turbo (decoded to YUV planes) |
| Audio | WASAPI / ASIO | SDL3 audio on PipeWire |
| UI | Win32 + Direct2D | Dear ImGui 1.92 (bundled), Pretendard font |
| Settings | `%LOCALAPPDATA%` | `~/.config/llcv/settings.ini` |
| Logs | `%LOCALAPPDATA%` | `~/.local/state/llcv/logs/` |
| Screenshots | Pictures | `$XDG_PICTURES_DIR/LLCV/` |

Not available on Linux: NVIDIA VSR, WASAPI Exclusive, ASIO, console LPCM 5.1,
and the vendor tone-mapping requests.

## Supported systems

| | Ubuntu 22.04 | Ubuntu 24.04 | Debian 13 / Ubuntu 26.04 | Arch Linux |
| --- | --- | --- | --- | --- |
| Package | `.deb` | `.deb` | `.deb` (`debian/`) | `PKGBUILD` (`packaging/arch/`) |
| SDL3 | bundled 3.4.18, static | bundled 3.4.18, static | system 3.2 or newer | system 3.2 or newer |
| Dear ImGui | bundled 1.92.2b | bundled 1.92.2b | bundled 1.92.2b | bundled 1.92.2b |
| HDR input (P010) | needs Linux 7.1+ | needs Linux 7.1+ | needs Linux 7.1+ | current kernels |
| HDR output | compositor too old (GNOME 42) | compositor too old (GNOME 46) | GNOME 48+ / KDE Plasma 6 | GNOME 48+ / KDE Plasma 6 |

| | Debian 12 (bookworm) | Debian 13 (trixie) |
| --- | --- | --- |
| Also for | LMDE 6, Raspberry Pi OS (bookworm) | Kali, Raspberry Pi OS (trixie) |
| Package | `.deb` amd64, arm64 | `.deb` amd64, arm64, armhf |
| SDL3 | bundled 3.4.18, static | system 3.2 or newer |
| HDR input (P010) | needs Linux 7.1+ | needs Linux 7.1+ |
| HDR output | compositor too old (GNOME 43) | GNOME 48 / KDE Plasma 6 (not the Raspberry Pi desktop) |

Linux Mint 21, Pop!_OS 22.04, Zorin OS 17 and elementary OS 7 are Ubuntu
22.04 based; use the Ubuntu 22.04 package. Linux Mint 22, Pop!_OS 24.04 and
Zorin OS 18 are Ubuntu 24.04 based; use the Ubuntu 24.04 package. A package
built on a newer release does not install on an older one. Raspberry Pi OS 64-bit uses the `arm64` package, and
Raspberry Pi OS 32-bit uses `armhf` (Raspberry Pi 2 or newer; Pi Zero and Pi 1
are not supported).

| | Fedora 43 or newer, Nobara | RHEL / Rocky / AlmaLinux 9 and 10 |
| --- | --- | --- |
| Package | `.rpm` (`packaging/rpm/llcv.spec`) | `.rpm` (same spec) |
| SDL3 | system 3.4 | bundled 3.4.18, static (EPEL has no SDL3) |
| HDR input (P010) | needs Linux 7.1+ | needs a P010-capable uvcvideo (not in EL 9/10 kernels) |
| HDR output | GNOME 48+ / KDE Plasma 6 | compositor too old (GNOME 40 / 47) |

When HDR is not available, the app says so and gives the reason. The reason
appears in Settings → Video → HDR, in the F1 help, in the Tab diagnostics,
and in a message when an HDR session starts. HDR signals are then shown as
SDR.

| GPU driver | SDR | HDR10 passthrough (Wayland) | Tone mapping |
| --- | --- | --- | --- |
| AMD / Intel (Mesa) | yes | yes, with a 10-bit EGL buffer | yes |
| NVIDIA (proprietary) | yes | when the driver offers a 10-bit EGL buffer on Wayland | yes |
| Raspberry Pi 4/5 (VideoCore, Mesa v3d) | yes, through OpenGL ES 3.0 | no (the Pi desktop has no color management) | yes |

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

## Build and install

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

## Menu bar

Press and release **Alt** during playback to show the menu bar. It holds the
View, Audio, Tools, Settings and Help commands, so you can use them without
remembering the function keys. Press Alt or Esc again, or click outside the
menu, to close it.

## Shortcuts

| Key | Action |
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

## Troubleshooting

- **No picture.** Close OBS and any other application that uses the capture
  device.
- **4K 60 fps.** The GC553Pro offers 4K 60 fps only as MJPEG. Uncompressed
  NV12 is limited to 4K 30 fps, and P010 to 1440p 30 fps.
- **Washed-out colors with an HDR source.** The source is sending HDR while an
  8-bit format is captured. Select P010, use **Force HDR10**, or turn off HDR
  on the source.
- **No sound.** Check **Audio → Capture audio device**. Auto mode selects the
  audio device of the same USB device as the video.
- **Crackling audio.** Raise the PCM buffer target in 5 ms steps.
- **Permission denied on /dev/video\*.** Log in at the local seat, or add your
  user to the `video` group.

## Updates

Packages are ready for an APT update repository, but nothing is published
yet: the signing key and address will be agreed with the original author
first. See [packaging/README.md](packaging/README.md) for the release
checklist. Release packages are built on the oldest supported release of each
family, so one file serves the newer releases too, and file names carry no
distribution name.

## License

GPL-3.0-or-later. Dear ImGui is MIT licensed. Pretendard is licensed under the
SIL Open Font License 1.1.

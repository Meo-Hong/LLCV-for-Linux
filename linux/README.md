# LLCV for Linux

> [한국어](README.ko.md)

LLCV shows video and audio from USB (UVC/V4L2) HDMI capture devices with low
latency. This is a Linux rewrite, by Meo-hong, of the Windows LLCV 2.1.4 viewer
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

## What's new in 2.1.4

- **F2** opens the default settings page (Video, or Audio in audio-only mode);
  recording options stay on the Recording tab.
- The last video and audio packets are no longer lost when a recording stops.
- Invalid numbers in `settings.ini` fall back to the defaults.

Full list: [release notes](release-notes/v2.1.4.md). 2.1.3 added
[recording](#recording) (F9), [playback mute](#playback-mute) (M), unlisted
30 fps requests and fullscreen video size and position; see the
[2.1.3 notes](release-notes/v2.1.3.md) and the
[2.1.3.1 notes](release-notes/v2.1.3.1.md).

| Area | Windows build | Linux build |
| --- | --- | --- |
| Video capture | DirectShow | V4L2 memory-mapped buffers, latest frame first |
| Presentation | D3D11 / DXGI | OpenGL 3.3 (EGL on Wayland, GLX on X11), or OpenGL ES 3.0 |
| Color conversion | D3D11 Video Processor | GLSL shaders, BT.601/709/2020, limited/full range |
| HDR10 | P010 + DXGI HDR10 | P010 + Wayland `wp_color_management_v1`, or tone mapping |
| MJPEG | Media Foundation | libjpeg-turbo (decoded to YUV planes) |
| Audio | WASAPI / ASIO | SDL3 audio on PipeWire |
| UI | Win32 + Direct2D | Dear ImGui 1.92 (bundled), Pretendard font; Korean, English, Japanese, Spanish and Brazilian Portuguese |
| Settings | `%LOCALAPPDATA%` | `~/.config/llcv/settings.ini` |
| Logs | `%LOCALAPPDATA%` | `~/.local/state/llcv/logs/` |
| Screenshots | Pictures | `$XDG_PICTURES_DIR/LLCV/` |
| Recording | Media Foundation | FFmpeg: NVENC, VA-API, x264, x265, SVT-AV1 |
| Recordings | Videos | `$XDG_VIDEOS_DIR/LLCV/` |
| Updates | In-app update check | APT repository (`.deb`) |

Not available on Linux: NVIDIA VSR, WASAPI Exclusive, ASIO, console LPCM 5.1,
the exclusive fullscreen and Compatibility (Blt) presentation modes (see
[Fullscreen](#fullscreen)), window edge snapping and rounded corners (the
compositor or window manager handles them), and the in-app update check (the
package manager handles updates).

## Install

Download the file for your system from
[Releases](https://github.com/Meo-Hong/LLCV-for-Linux/releases/latest).

| File | Install on | Command |
| --- | --- | --- |
| `llcv_2.1.4_amd64.deb` | Ubuntu 22.04 to 26.04, Debian 12 and 13, Linux Mint, Pop!_OS, Zorin OS, elementary OS, LMDE, Kali | `sudo apt install ./llcv_2.1.4_amd64.deb` |
| `llcv_2.1.4_arm64.deb` | Raspberry Pi OS 64-bit, Debian 12 and 13 (arm64) | `sudo apt install ./llcv_2.1.4_arm64.deb` |
| `llcv_2.1.4_raspberrypi-32bit_armhf.deb` | Raspberry Pi OS 32-bit (trixie) | `sudo apt install ./llcv_2.1.4_raspberrypi-32bit_armhf.deb` |
| `llcv-2.1.4-1.x86_64.rpm` | Fedora, Nobara, RHEL, Rocky Linux, AlmaLinux 9 and 10 | `sudo dnf install ./llcv-2.1.4-1.x86_64.rpm` |
| `llcv-2.1.4-1-x86_64.pkg.tar.zst` | Arch Linux, Manjaro, EndeavourOS | `sudo pacman -U ./llcv-2.1.4-1-x86_64.pkg.tar.zst` |

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
| Ubuntu 22.04, Linux Mint 21, Pop!_OS 22.04, Zorin OS 17, elementary OS 7 | amd64 `.deb` | no (GNOME 42) | build, install check, recording check (container) |
| Debian 13, Kali | amd64 `.deb` | yes (GNOME 48 / KDE Plasma 6) | install check |
| Debian 12, LMDE 6 | amd64 `.deb` | no (GNOME 43) | install check |
| Raspberry Pi OS 64-bit (bookworm, trixie) | arm64 `.deb` | no | build |
| Raspberry Pi OS 32-bit (trixie) | armhf `.deb` | no | build |
| Fedora 43 or newer, Nobara | `.rpm` | yes (GNOME 48 or newer / KDE Plasma 6) | build, install check, recording check (Fedora 44, container) |
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

### Unlisted 30 fps requests

**Video → Capture → Allow unlisted FPS (30 fps)** is off by default. When it is
checked and the driver does not list 30 fps for the selected resolution and
format, the frame-rate list gains **30 fps · direct request**, shown in the
accent color.

- It is a direct request, not a support confirmation. On **Start**, LLCV checks
  the rate the driver set. If the driver rejects 30 fps or applies another
  rate, startup stops with a notice instead of using another rate.
- Turn off 40 fps capture first. It does not convert 60 fps to 30 fps in
  software.
- After starting, check **Actual FPS** in the Tab diagnostics.

## Recording

**F9** records the playing video and capture audio to MP4. Press it again to
stop and finish the file. The options are under **Settings → Recording**.

- **Input.** NV12, YUY2, MJPEG and BGR24 SDR video with 16-bit stereo capture
  audio. HDR input (P010 or Force HDR10) is not recorded. MJPEG is decoded a
  second time for the recording, which uses more CPU.
- **Codec and encoder.** H.264, HEVC or AV1. **Automatic hardware** tries
  NVIDIA NVENC, then VA-API (AMD, Intel). **CPU** uses x264, x265 or SVT-AV1
  from the system FFmpeg. Opening the Recording page checks which encoders
  work and greys out the rest. An unsupported combination reports an error
  instead of switching codec or falling back to the CPU.
- **Defaults.** H.264, automatic hardware, quality mode (constant QP 23),
  1080p and 60 fps limits, AAC 192 kbps, recording volume 100 % and no
  splitting. The limits never raise the source size or rate, and they do not
  change playback.
- **Quality.** Smaller files (QP 26), balanced (QP 23) or higher quality
  (QP 20); AV1 uses an equivalent quantizer. Target bitrate mode offers
  2 to 100 Mbps.
- **Recording volume** (0 to 100 %) is separate from playback volume and
  playback mute. 0 % records silence.
- **Automatic splitting** by duration (15, 30, 60 or 120 minutes) or size (1,
  2, 4, 8 or 16 GiB). A new file starts at the next keyframe, so a part can run
  slightly past the limit.
- **Files.** Recordings go to `$XDG_VIDEOS_DIR/LLCV/` (usually `~/Videos/LLCV`)
  as `LLCV_YYYYMMDD_HHMMSS.mp4`; later parts end in `_part02`, `_part03` and so
  on. Another folder can be chosen on the Recording page. The files are
  fragmented MP4, so parts already written usually survive a crash. The last
  seconds are not guaranteed.
- **Pause and resume** are in the menu bar (**Alt → Recording**). Paused time
  is left out of the file while playback continues.
- A corner indicator shows the recording time and size. The indicator, the
  other overlays, playback volume and playback mute are not recorded.
- Opening settings or quitting during a recording asks first, then stops the
  recording and saves it. Low disk space, write errors and an encoder that
  cannot keep up stop the recording with a notice; playback continues.

The `.deb` packages contain their own minimal FFmpeg 7.1.1, so they work on
every supported release: H.264 can be recorded on the CPU (x264), while HEVC
and AV1 need NVENC or VA-API. The Raspberry Pi OS 32-bit package has no x264
(the distribution's static x264 cannot be linked there), so recording is not
available on it. The Fedora `.rpm` and the Arch package use the
system FFmpeg (4.4 or newer when building). The RHEL, Rocky Linux and
AlmaLinux `.rpm` is built without FFmpeg and shows recording as unavailable.

### Recording tests (2.1.3)

Before the 2.1.3 release, and again for 2.1.3.1, recording was checked with test programs that link
the same recording, capture and settings code as the app. Containers have no
GPU, so they used the CPU encoders.

| Environment | What was checked | Result |
| --- | --- | --- |
| Ubuntu 26.04, GeForce RTX 2070, FFmpeg 8.0 | H.264 and HEVC (NVENC and CPU), AV1 (CPU); quality and bitrate modes; NV12, YUY2, MJPEG and BGR24 input; pause; 1440p 120 fps limited to 1080p 60 fps; recording volume 0 %; no audio; 1-minute splitting | All files saved and decoded. Paused time left out; the second part starts at a keyframe |
| Ubuntu 26.04, GC553Pro | NV12 1080p 60 fps and MJPEG 240 fps capture (device test screen, no HDMI source) | 60 fps recorded with no dropped frames; color range and matrix kept |
| Ubuntu 26.04, GC553Pro | Direct 30 fps request on modes without 30 fps | Stopped with a notice when the driver applied 59.94 fps; 29.97 fps accepted |
| Ubuntu 26.04 | Settings from 2.0.5 (F9 or M already used), saving and loading the new options | Existing keys kept; Recording and Mute got free keys |
| Ubuntu 22.04 container, FFmpeg 4.4 | `.deb` build, install, run; x264 recording in both modes | Passed |
| Fedora 44 container, `ffmpeg-free` 8.1 | `.rpm` build, install, run; SVT-AV1 recording | Passed. x264 and x265 are not in `ffmpeg-free` |
| Fedora 44 container with RPM Fusion FFmpeg (Nobara setup) | Same `.rpm`; x264, x265 and SVT-AV1 in both modes | Passed |
| Build without FFmpeg (RHEL family) | Compile | Passed |
| 2.1.3.1: Ubuntu 22.04, 24.04 and 26.04, Debian 12 and 13 containers | One amd64 `.deb` (built on 22.04) installed and run on each | Passed. FFmpeg and x264 are linked statically; only libva comes from the system |
| 2.1.3.1: Ubuntu 26.04, GeForce RTX 2070, FFmpeg 7.1.1 built into the `.deb` | NVENC H.264 and HEVC, x264 recording | Passed, no dropped frames |
| 2.1.3.1: arm64 and armhf `.deb` | Cross build | Passed with no FFmpeg dependency (armhf has VA-API only, without x264 and NVENC); install checks run in CI under QEMU |

Not checked yet: VA-API encoding on AMD and Intel, NVENC on other
distributions, sound level and A/V sync with a live HDMI source, and the
recording UI (Recording page, indicator, confirm dialog), which was built but
not operated in these tests. The hardware tone-mapping requests (see [HDR](#hdr))
were not checked on a real device either: the GC553Pro has no such control, so
only the HID report layout was compared with the Windows build.

## Playback mute

**M** mutes playback without changing the volume. The speaker button in the
audio OSD (F3) and in the audio-only view, and **Alt → Audio → Mute playback**,
do the same. While the audio OSD is hidden, a translucent speaker icon stays at
the top right of the video.

- Recordings keep their sound.
- Turning the mouse wheel, or volume up/down in the menu, unmutes playback.
- Mute is cleared when LLCV quits, and it works independently of **Mute
  automatically in background**.

## Audio controls and the audio-only view

- Double-click the master, left or right volume control in the audio OSD (F3)
  or the audio-only view to reset it to 100 %. This also unmutes playback.
- Turn the mouse wheel over the left or right control to change only that
  channel; over the master control or anywhere else it changes the master volume.
- Volumes above 100 % (with **Allow volume boost** on) are shown in orange.
- Drag inside the audio-only view, outside the controls, to move the window.
  Its size keeps the aspect ratio. **Window → Borderless window** also hides
  the title bar of the audio-only view; drag its edges to resize it.

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

**Video → HDR → HDR chroma placement** stays on **Auto** unless color edges of
P010 input look shifted; **Left** and **Top-left** are compatibility overrides
for devices that place the chroma samples differently.

At each start LLCV asks a supported capture device to tone map HDR to SDR
itself for 8-bit formats (NV12, YUY2, MJPEG, BGR24), and to pass HDR through
for P010 or **Force HDR10**, like the Windows build. This covers AVerMedia
devices with the AVerMedia UVC extension control and the Elgato HD60 S+, HD60 X
and HD60 X Rev.2. The packages install a udev rule that lets the logged-in user
reach the Elgato HID interface; reconnect the device once after installing. The GC553Pro has no such control, so nothing is
sent to it. A request the device accepts does not prove the input encoding;
the diagnostic log shows the result in `[capture-hdr]` lines.

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

## Fullscreen

**Window → Fullscreen video size** chooses **Fit to screen** (default) or
**Original size**, which shows the capture 1:1 and shrinks it only when it is
larger than the screen. **Window → Fullscreen video position** places the video
in the center, at an edge or in a corner when it does not fill the screen, for
example 16:9 video on an ultrawide monitor. Margins stay black, and the video
is never cropped.

When the capture resolution matches the monitor (for example 2560 × 1440 on a
1440p monitor), playback starts in fullscreen, and **F5** uses fullscreen too,
because that is the only way to show those pixels 1:1. Otherwise F5 restores a
1:1 window and says so when it has to shrink the window to fit the monitor.

**Window → Keep relative window size when moving monitors** (off by default)
keeps a similar share of the screen when the window moves to a monitor with a
different size. Press F5 afterwards for exact 1:1 again.

The Windows build's exclusive fullscreen mode has no Linux counterpart. Wayland
compositors can show a fullscreen window directly, without composition, and on
X11 SDL asks the compositor to leave fullscreen windows uncomposited.

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
View, Audio, Recording, Tools, Settings and Help commands, so you can use them
without remembering the function keys. **Recording → Open last recording**
opens the most recent file in the default player.

Settings opened during playback (F2 or the menu) start on the Video page, or
Audio in audio-only mode. They have a **Cancel** button that
discards the changes and resumes playback with the previous settings. Press Alt or the Exit key (default Esc) again,
or click outside the menu, to close it.

## Shortcuts

The keys below are the defaults. They can be changed under
**Settings → Guide & logs**.

| Default key | Action |
| --- | --- |
| Alt | Show or hide the menu bar |
| F1 | App information and shortcuts |
| F2 | Stop playback and open settings (Video page; Audio in audio-only mode) |
| F3 | Audio meters and volume |
| F5 | Restore the 1:1 source size (fullscreen when the capture matches the monitor) |
| F11 / Alt+Enter | Fullscreen |
| F12 | PNG screenshot at the capture resolution |
| Tab | Live diagnostics |
| Esc | Leave fullscreen, or exit |
| F9 | Start or stop recording |
| M | Mute or unmute playback |
| Mouse wheel | Volume ±5% (also unmutes) |
| Double-click | Toggle fullscreen |

### Changing shortcuts

1. Open **Settings → Guide & logs**, then click the key button beside an
   action.
2. Press a key, optionally with **Ctrl**, **Alt** or **Shift**. Click the same
   button again to cancel.
3. Choose **Start**. The new keys are saved with the other settings.

All ten shortcuts can be changed, including **Leave fullscreen / exit**
(Esc). None can be empty. Duplicate keys, modifier-only keys, Super (Windows
key) combinations and reserved combinations such as Alt+F4, Alt+Tab,
Ctrl+Alt+Delete and Ctrl+Alt+F1 to F12 cannot be assigned. **Reset defaults**
restores all ten. Alt (menu bar) and Alt+Enter (fullscreen) stay fixed.

When a profile from an earlier version already uses F9 or M for another action,
that key is kept, and Start / stop recording or Toggle playback mute gets an
unused function key instead. Check the assigned keys under **Guide & logs**.

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
- **Recording is unavailable or greyed out.** The package was built without
  FFmpeg (RHEL family), or no encoder works for the chosen codec. With the
  `.deb` packages, CPU recording is H.264 only. For hardware
  encoding install the VA-API driver (`intel-media-va-driver` for Intel,
  `mesa-va-drivers` for AMD) or the NVIDIA driver, or choose **CPU**. The
  diagnostic log lists the result of each encoder check. Fedora's default
  `ffmpeg-free` has no x264 or x265, so CPU H.264 and HEVC are unavailable
  there; SVT-AV1 and the hardware encoders still work. Switching to the RPM
  Fusion FFmpeg (`sudo dnf swap ffmpeg-free ffmpeg --allowerasing`) adds them,
  and Nobara ships it already. On AMD, Fedora also needs
  `mesa-va-drivers-freeworld` from RPM Fusion for H.264 and HEVC.
- **Recording stops with "could not keep up".** Lower **Resolution limit** or
  **Frame-rate limit**, or use a hardware encoder.
- **Unlisted 30 fps will not start.** The driver rejected 30 fps or applied
  another rate. Read the `Direct 30 fps request` line in the diagnostic log,
  then choose a listed rate or uncheck **Allow unlisted FPS**.
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
by Meo-hong. Dear ImGui is MIT licensed. Pretendard and the Noto Sans JP kanji
subset are licensed under the SIL Open Font License 1.1.

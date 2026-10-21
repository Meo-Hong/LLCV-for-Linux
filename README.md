# LLCV - Low Latency Capture Viewer for Windows and Linux

> [한국어](README.ko.md)

LLCV shows video and audio from HDMI capture devices with low latency. Stale
frames are dropped in favor of the newest one.

> **Maintained separately from the original LLCV.** This repository is run by
> Meo-hong. The Linux port in `linux/` is developed, released and supported
> here, not by the original project, and is not an official release of it.
> The original Windows LLCV by
> [seria-aa](https://github.com/seria-aa/LowLatencyCaptureViewer) is kept
> unchanged in `windows/` for reference.

| Folder | Contents |
| --- | --- |
| [`linux/`](linux/README.md) | Linux port by Meo-hong: V4L2 capture, OpenGL, PipeWire audio, HDR10 on Wayland, MP4 recording. Packages for Ubuntu, Debian, Raspberry Pi OS, Fedora, Nobara, RHEL and Arch |
| [`windows/`](windows/README.md) | The original Windows LLCV 2.1.4 by seria-aa, unchanged: DirectShow capture, D3D11 presentation, WASAPI and ASIO audio |

## Linux

Current release: **2.1.4** ([Releases](https://github.com/Meo-Hong/LLCV-for-Linux/releases/latest)) ·
F2 opens the default settings page, no lost packets at the end of a recording
([release notes](linux/release-notes/v2.1.4.md))

| File | Install on |
| --- | --- |
| `llcv_2.1.4_amd64.deb` | Ubuntu 22.04 to 26.04, Debian 12 and 13, and their derivatives |
| `llcv_2.1.4_arm64.deb` | Raspberry Pi OS 64-bit, Debian 12 and 13 (arm64) |
| `llcv_2.1.4_raspberrypi-32bit_armhf.deb` | Raspberry Pi OS 32-bit (trixie) |
| `llcv-2.1.4-1.x86_64.rpm` | Fedora, Nobara, RHEL, Rocky Linux, AlmaLinux 9 and 10 |
| `llcv-2.1.4-1-x86_64.pkg.tar.zst` | Arch Linux, Manjaro, EndeavourOS |

- The `.deb` packages update themselves through the APT repository
  `https://meo-hong.github.io/LLCV-for-Linux/`.
- Tested on real hardware: Ubuntu 26.04 with an AVerMedia GC553Pro and an
  NVIDIA GeForce RTX 2070. Other systems are covered by build and install
  checks.
- Overview, supported systems and usage: [linux/README.md](linux/README.md)
- Building from source: [linux/BUILDING.md](linux/BUILDING.md)
- Linux problems: [Issues](https://github.com/Meo-Hong/LLCV-for-Linux/issues)

## Windows

The Windows sources are kept as published by the original project and are not
developed here. For Windows downloads and support, use the
[original repository](https://github.com/seria-aa/LowLatencyCaptureViewer/releases/latest).
Changes from the original project may be merged into `windows/`.

## License

GPL-3.0-or-later ([LICENSE](LICENSE)). The original Windows LLCV is by
seria-aa; the Linux port is by Meo-hong. Bundled third-party components keep
their own licenses, listed in each folder.

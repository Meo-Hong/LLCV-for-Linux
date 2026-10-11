# LLCV - Low Latency Capture Viewer for Windows and Linux

> [한국어](README.ko.md)

LLCV shows video and audio from HDMI capture devices with low latency. Stale
frames are dropped in favor of the newest one.

| Folder | Contents |
| --- | --- |
| [`linux/`](linux/README.md) | Linux port by Meo-hong: V4L2 capture, OpenGL, PipeWire audio, HDR10 on Wayland. Packages for Ubuntu, Debian, Raspberry Pi OS, Fedora, RHEL and Arch |
| [`windows/`](windows/README.md) | The original Windows LLCV 2.0.2 by [seria-aa](https://github.com/seria-aa/LowLatencyCaptureViewer), unchanged: DirectShow capture, D3D11 presentation, WASAPI and ASIO audio |

## Linux

- Overview and supported systems: [linux/README.md](linux/README.md)
- Building and installing: [linux/BUILDING.md](linux/BUILDING.md)
- Packages: [Releases](https://github.com/Meo-Hong/LLCV-for-Linux/releases)

## Windows

The Windows sources are kept as published by the original project. For
Windows downloads and support, use the
[original repository](https://github.com/seria-aa/LowLatencyCaptureViewer/releases/latest).
Changes from the original project are merged into `windows/`.

## License

GPL-3.0-or-later ([LICENSE](LICENSE)). The original Windows LLCV is by
seria-aa; the Linux port is by Meo-hong. Bundled third-party components keep
their own licenses, listed in each folder.

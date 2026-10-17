<img align="right" src="images/app-icon.png" width="40" height="40" alt="LLCV">

# Building from source

[README](../README.md) · [Documentation](INDEX.md) · [한국어](BUILDING.ko.md)

[Requirements](#requirements) · [Release build](#release-build) · [Installer and portable ZIP](#installer-and-portable-zip)

<br>

## Requirements

- Windows 10 or 11 x64
- Visual Studio 2022 with **Desktop development with C++**
- Windows SDK and CMake tools for Windows
- Git for Windows (Git Bash), curl and tar to build the FFmpeg mux dependency
- Inno Setup 7 only when building the installer

The project uses Windows system APIs and libraries: Win32, DirectShow, D3D11,
DXGI, Media Foundation and WASAPI. Recording also requires a pinned mux-only FFmpeg
shared-library build. Media Foundation provides encoding; no FFmpeg program or codec pack is required.

<br>

## Release build

Open an x64 Visual Studio developer PowerShell in the repository root:

```powershell
chcp.com 65001 > $null
.\tools\Build-RecordingFFmpeg.ps1
cmake -S . -B build -A x64 -DLLCV_RECORDING_FFMPEG_ROOT="$PWD/outputs/dependencies/recording-ffmpeg/install"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The executable is generated at:

```text
build\Release\LowLatencyCaptureViewer.exe
```

The release configuration uses the static MSVC runtime, so the packaged
executable does not require a separate Visual C++ Redistributable installation.

The 2.1.0 suite covers recording, splitting, pause/resume, storage delays, crash-tolerant files,
exclusive fullscreen and video placement. Existing checks cover 40 fps negotiation, color
metadata, settings pages, device transitions, save failures, stream completion,
and power-request lifetimes. File-save tests need a temporary
directory that permits writing and renaming files. Record tests skipped because
a GPU or Windows feature is unavailable separately from passes. Power API tests
do not put the PC to sleep and do not replace real sleep/resume validation.

Independent decode, segment and sync checks also require Python 3 and test-only
ffmpeg/ffprobe programs. Put them on PATH or set `LLCV_TEST_FFMPEG` and
`LLCV_TEST_FFPROBE` in CMake. Without them these checks are not registered; do not
report that as complete coverage. These programs are never included in packages.

<br>

A playback-only developer build can use `-DLLCV_RECORDING=OFF` without FFmpeg; it is not the 2.1.0 release package.
The mux runtime and `ffmpeg-licenses` folder must remain beside the executable.

## Installer and portable ZIP

After the Release build, run these commands from the repository root. These
examples package v2.1.0 using the `build\Release` output above; the build-directory
override is required because the release scripts normally use a versioned
build directory.

```powershell
chcp.com 65001 > $null
& "C:\Program Files\Inno Setup 7\ISCC.exe" `
  "--define=BuildDir=..\build\Release" ".\installer\LowLatencyCaptureViewer.iss"
powershell -NoProfile -ExecutionPolicy Bypass -File .\tools\package-v1.ps1 `
  -Version 2.1.0 -BuildDir ..\build\Release -OutputDir outputs\v2.1.0
```

The outputs are `outputs\v2.1.0\LowLatencyCaptureViewer_v2.1.0_Setup.exe`
and `outputs\v2.1.0\LowLatencyCaptureViewer_v2.1.0_x64.zip`. When preparing
another version, update the versioned release files first; changing a ZIP name
does not update the executable. See the [release checklist](RELEASING.md).

Both packages must include `LICENSE`, `ASIO-SDK-LICENSE.txt`,
`ASIO-HOST-LICENSE.txt`, and `PRETENDARD-LICENSE.txt`. They must not contain machine-specific `settings.ini`
files, diagnostic logs, test executables, PDB files, or build directories.
Automated tests do not replace real-device or long-duration playback checks;
record separately which hardware checks were actually performed.

Runtime settings and logs are stored per user under:

```text
%LOCALAPPDATA%\LowLatencyCaptureViewer
```

An older `settings.ini` beside the executable is copied to that location on the
first launch and is not deleted automatically.

---

[README](../README.md) · [Documentation](INDEX.md) · [한국어](BUILDING.ko.md)

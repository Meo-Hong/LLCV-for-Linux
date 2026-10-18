%global sdl3_version 3.4.18
%global imgui_version 1.92.2b
%global app_id io.github.meo_hong.LLCV

%if 0%{?fedora}
%bcond_with bundled_sdl3
%bcond_without recording
%else
%bcond_without bundled_sdl3
%bcond_with recording
%endif

Name:           llcv
Version:        2.1.3.1
Release:        1
Summary:        Low-latency viewer for USB HDMI capture devices

License:        GPL-3.0-or-later AND MIT AND OFL-1.1%{?with_bundled_sdl3: AND Zlib}
URL:            https://github.com/Meo-Hong/LLCV-for-Linux
Source0:        llcv-%{version}.tar.gz
%if %{with bundled_sdl3}
Source1:        https://github.com/libsdl-org/SDL/releases/download/release-%{sdl3_version}/SDL3-%{sdl3_version}.tar.gz
%endif

BuildRequires:  cmake >= 3.22
BuildRequires:  gcc-c++
BuildRequires:  make
BuildRequires:  pkgconfig(libturbojpeg)
BuildRequires:  pkgconfig(libpng)
BuildRequires:  pkgconfig(wayland-client)
BuildRequires:  pkgconfig(wayland-scanner)
BuildRequires:  wayland-protocols-devel
BuildRequires:  pkgconfig(dbus-1)
%if %{with recording}
BuildRequires:  pkgconfig(libavcodec)
BuildRequires:  pkgconfig(libavformat)
BuildRequires:  pkgconfig(libavutil)
BuildRequires:  pkgconfig(libswscale)
%endif
%if %{with bundled_sdl3}
BuildRequires:  pkgconfig(alsa)
BuildRequires:  pkgconfig(egl)
BuildRequires:  pkgconfig(gl)
BuildRequires:  pkgconfig(libdecor-0)
BuildRequires:  pkgconfig(libpipewire-0.3)
BuildRequires:  pkgconfig(libpulse)
BuildRequires:  pkgconfig(wayland-cursor)
BuildRequires:  pkgconfig(wayland-egl)
BuildRequires:  pkgconfig(x11)
BuildRequires:  pkgconfig(xcursor)
BuildRequires:  pkgconfig(xext)
BuildRequires:  pkgconfig(xfixes)
BuildRequires:  pkgconfig(xi)
BuildRequires:  pkgconfig(xkbcommon)
BuildRequires:  pkgconfig(xrandr)
%if 0%{?rhel} && 0%{?rhel} < 10
BuildRequires:  pkgconfig(xscrnsaver)
%endif
BuildRequires:  pkgconfig(xtst)
%else
BuildRequires:  pkgconfig(sdl3) >= 3.2
%endif

Requires:       hicolor-icon-theme
Requires:       libglvnd-egl
Requires:       libglvnd-glx
Recommends:     pipewire
%if %{with bundled_sdl3}
Recommends:     libdecor
%endif
Suggests:       v4l-utils

Provides:       bundled(imgui) = %{imgui_version}
%if %{with bundled_sdl3}
Provides:       bundled(SDL3) = %{sdl3_version}
%endif

%description
LLCV shows video and audio from UVC (V4L2) HDMI capture devices such as the
AVerMedia Live Gamer ULTRA S GC553Pro with minimal delay. Only the newest
captured frame is presented, using OpenGL for color conversion and scaling, and
capture audio is played through PipeWire with a small drift-corrected buffer.

Features include NV12, YUY2, MJPEG, BGR24 and P010 (HDR10) capture modes, HDR10
passthrough on Wayland compositors with color management, HDR to SDR tone
mapping everywhere else, an audio-only mode, PNG screenshots, MP4 recording of
SDR video through FFmpeg (Fedora builds) and a live diagnostics overlay. P010
capture needs Linux 7.1 or newer.

%prep
%autosetup -n llcv-%{version}
%if %{with bundled_sdl3}
mkdir -p third_party/sdl3
tar -xzf %{SOURCE1} -C third_party/sdl3
%endif
cp -p third_party/imgui/LICENSE.txt imgui-LICENSE.txt
cp -p third_party/pretendard/LICENSE.txt Pretendard-LICENSE.txt
cp -p third_party/noto-sans-jp/LICENSE.txt NotoSansJP-LICENSE.txt

%build
%cmake \
    -DLLCV_SOURCE_TREE_DATA=OFF \
    -DLLCV_SDL3=%{?with_bundled_sdl3:BUNDLED}%{!?with_bundled_sdl3:SYSTEM} \
    -DLLCV_WAYLAND_COLOR_MANAGEMENT=ON \
    -DLLCV_SLEEP_INHIBIT=ON \
    -DLLCV_RECORDING=%{?with_recording:ON}%{!?with_recording:OFF}
%cmake_build

%install
%cmake_install

%files
%license LICENSE imgui-LICENSE.txt Pretendard-LICENSE.txt NotoSansJP-LICENSE.txt
%doc README.md README.ko.md
%{_bindir}/llcv
%{_datadir}/applications/%{app_id}.desktop
%{_datadir}/metainfo/%{app_id}.metainfo.xml
%{_datadir}/icons/hicolor/*/apps/%{app_id}.png
%{_datadir}/llcv/
%{_mandir}/man1/llcv.1*
%{_prefix}/lib/udev/rules.d/70-llcv-elgato-hid.rules

%changelog
* Mon Oct 19 2026 Meohong <busimh001@gmail.com> - 2.1.3.1-1
- Version kept in step with the .deb fix release (the .deb packages now
  contain a static FFmpeg). No change to the RPM build.

* Mon Oct 19 2026 Meohong <busimh001@gmail.com> - 2.1.3-1
- Follow the Windows LLCV 2.1.0 to 2.1.3 updates.
- MP4 recording (F9) of NV12, YUY2, MJPEG and BGR24 SDR video with stereo
  AAC audio through FFmpeg: H.264, HEVC or AV1 on NVENC, VA-API or CPU
  encoders, quality or bitrate control, resolution/frame-rate limits,
  separate recording volume, pause/resume and automatic splitting. Built on
  Fedora; RHEL-family builds have no FFmpeg and show recording as
  unavailable.
- Playback mute (M) from the audio OSD, the audio-only view or the menu, with
  a corner icon while the audio OSD is hidden; recordings keep their sound.
- Optional direct 30 fps request when the driver does not list it.
- Fullscreen video size (fit or original) and position settings.
- The window position is no longer saved while the window is minimized.
- More Windows features: HDR chroma placement, hardware HDR-to-SDR requests
  for AVerMedia UVC-extension and Elgato HD60 S+/X devices, fullscreen when
  the capture matches the monitor, relative window size across monitors,
  volume reset by double-click, per-channel wheel, a movable audio-only view,
  Open last recording and Cancel in settings.

* Wed Oct 14 2026 Meohong <busimh001@gmail.com> - 2.0.5-1
- Follow the Windows LLCV 2.0.5 update.
- Customizable keyboard shortcuts with Ctrl/Alt/Shift combinations and
  Reset defaults; help, menu and overlays show the assigned keys.

* Wed Oct 14 2026 Meohong <busimh001@gmail.com> - 2.0.4-1
- Follow the Windows LLCV 2.0.3 and 2.0.4 updates.
- Optional 40 fps capture, confirmed against the rate the driver reports,
  without falling back to another rate.
- Prevent sleep during playback (on by default) through SDL and the desktop
  portal; audio-only lets the display turn off.
- A failed settings save keeps the previous file and shows a notice.

* Sat Oct 10 2026 Meohong <busimh001@gmail.com> - 2.0.2-1
- First RPM package of the Linux port: V4L2 capture, OpenGL presentation,
  SDL3 audio on PipeWire, HDR10 passthrough and tone mapping.
- SDL3 3.4.18 is linked statically on RHEL, Rocky and AlmaLinux, which do not
  ship SDL3.

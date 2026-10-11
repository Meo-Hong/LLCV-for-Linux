# LLCV - Low Latency Capture Viewer

> [한국어](README.ko.md) · [Download the latest release](https://github.com/seria-aa/LowLatencyCaptureViewer/releases/latest)

LLCV is a lightweight Windows viewer for showing HDMI capture-device video and audio
with low latency. It receives video through DirectShow, presents it directly
with D3D11, and sends capture audio to the chosen output device. Stale video
frames are discarded in favor of the latest frame, making it well suited to a
capture-device window used alongside other work or viewed directly.

No FFmpeg, codec pack, or separate Visual C++ Redistributable is required.

## What's new in 2.0.2

See the [2.0.2 release notes](docs/release-notes-v2.0.2.md) for the changes.

- Unified the app name and viewer title as **LLCV**. Detailed playback information is available with Tab.
- Simplified capture-device and HDR10/MJPEG/ASIO/console LPCM 5.1 labels.
- Improved WASAPI Exclusive device checks and result reuse. Completed results survive closing settings mid-scan, and valid user-selected output buffers are preserved.
- Completed the English Exclusive device-status and rescan guidance.
- Added installation choices for Start Menu and desktop shortcuts, with improved DPI-aware spacing.
- Reduced unnecessary work and allocations in audio-buffer copying, optional diagnostics, and English text lookup, retaining latest-frame-first presentation and audio defaults.

## Features at a glance

| Feature | What it offers |
| --- | --- |
| Low-latency video | Latest-frame-first presentation; Immediate, VSync, and Compatibility output |
| Video formats | NV12/YUY2, MJPEG compatibility mode, and P010 HDR10 input |
| NVIDIA VSR | Experimental SDR / HDR10 enhancement requests at native size or larger; independent input/display sizes and F6 toggle |
| Audio outputs | WASAPI Shared/Exclusive, ASIO with an installed driver, and following the Windows default output device |
| Console LPCM 5.1 | 5.1 playback on supported equipment; WASAPI Shared only |
| Audio-only view | Audio without video, with master/L/R volume, level meters, and clipping status |
| App theme | Shared dark/light colors and Pretendard typography across settings, F1 help, and audio-only view |
| Window controls | 1:1 display, aspect-ratio resizing, fullscreen/borderless, startup monitor selection, and edge snap |
| Screenshots and help | F12 source-resolution PNGs, optional clipboard copy, HDR-to-SDR export, and F1 app information/shortcuts |
| Convenience and diagnostics | Automatic clock correction, optional 200% volume boost/background mute, diagnostic logs, update checks, Korean/English |

## Download

Get one of the files from the [latest release](https://github.com/seria-aa/LowLatencyCaptureViewer/releases/latest):

- **Setup.exe — recommended:** installs the app and an uninstaller. Start Menu
  and desktop shortcuts are optional choices during setup.
- **x64.zip — portable:** extract it, then run `LowLatencyCaptureViewer.exe`.

Windows 10/11 x64 and a capture-device driver are required.

## First use

1. Close OBS, the vendor capture utility, and any other application using the
   capture device.
2. Start the app.
3. Confirm **Capture device**, then select **Start**.

The defaults are a good first test: 1080p, low-latency presentation, WASAPI
Shared, a 25 ms PCM buffer, and automatic clock-drift correction. A saved 20 ms
PCM target from an older build is upgraded to 25 ms once on first launch; other
saved values are preserved. You can select 20 ms again afterward.

If video works but audio does not, check **Capture audio device**. USB capture
devices can expose separate video and audio devices; in that case, choose the
matching audio input manually. If the app shows “Internal audio detected · use
automatically,” no separate selection is needed.

## Finding your settings

The sidebar separates **Video**, **Audio**, **Window**, **Guide & logs**, and **App**.
Video contains capture/display settings, VSR, and screenshot options at the bottom.
Audio contains output devices, buffers, volume options, audio-only mode, and LPCM 5.1.
Window controls placement, resizing, borders, and cursor behavior.
Guide & logs groups keyboard shortcuts and diagnostic logging; App contains language,
theme, startup, and update preferences.

Choose **Dark** or **Light** under **App → App theme**. Settings, F1 help, and
the audio-only view share the theme. Overlays drawn over video remain dark for
contrast against the picture, regardless of the app theme.

Supported format/FPS combinations are queried in the background and reused while
the settings window remains open. **Refresh** requests a new query. Start remains
unavailable while the required video capabilities are being checked or could not be queried.

## What should I choose?

### Video

| Setting | Good starting choice |
| --- | --- |
| Capture resolution | **1920 × 1080** to start. With VSR enabled, this field becomes **Display resolution**; set input size separately in **VSR capture** |
| Pixel format | **Auto (NV12 preferred)** |
| Frame rate | **Auto**, or the source's actual output rate |
| Presentation | **Immediate (minimum latency)**; **VSync (reduced tearing)** waits for refresh. **Compatibility (Blt + VSync)** uses an alternative output path, may add latency/GPU load, and does not support HDR10 |
| Display monitor | **Auto (restore last position)**; select a monitor to choose the startup location. A missing monitor falls back to the primary display; moving the window afterward is still allowed |
| Pixel-perfect / Lock display size | Without VSR, locks the source-sized window. With VSR, locks the selected display size. Turn off to resize manually |

A 120 fps capture mode does not create extra visual information when the game
does not output a new frame at that rate. Choose only frame rates that the
source and capture device actually support.

### Audio

| Setting | Good starting choice |
| --- | --- |
| Audio output mode | **WASAPI Shared** |
| Output device | **Follow the Windows default output device** |
| Output buffer | The value marked as recommended in settings |
| PCM buffer target | **25 ms** |
| Clock-drift correction | **Auto** |
| Console LPCM 5.1 | **Off (default)**. WASAPI Shared only; requires multichannel capture and playback equipment |

WASAPI Shared is the default mode for compatibility with other applications
and Windows effects. ASIO appears only when an ASIO driver
is installed. WASAPI Exclusive playback requires an output device that passes
the app's playback-event check. Use WASAPI Shared unless you have a specific
reason to choose another mode.

If sound occasionally breaks up, open the Tab diagnostics overlay. Raise the
PCM target in 5 ms steps from its current value (e.g. `20 → 25 → 30 ms`) only
when **buffer shortage** or **resampler output shortage** repeats.
Leave it alone when there are no errors.

### Using WASAPI Exclusive

Select **WASAPI Exclusive** as the audio output mode to check output devices
that have no saved result. Use an available device with an output buffer at
or above its verified minimum.

- Completed results are saved per device. Closing settings while other devices are still being checked retains results that have already completed.
- **Retry required** means the check could not establish compatibility, for example because the device was busy, disconnected, or had unstable timing. It does not trigger repeated automatic checks; inspect the device, then choose **Recheck all devices** to retry.
- Another device completing its check preserves the current device's valid output-buffer choice. Only invalid choices, such as a value below the verified minimum, are adjusted.
- Version 2.0.2 refreshes older probe records once. Results completed under the new criteria are reused across mode switches and launches.

Exclusive reserves the output device, which can limit simultaneous use by other
apps and bypass Windows audio effects. Console LPCM 5.1 requires **WASAPI Shared**.
See the [audio guide](docs/AUDIO.md) for details.

### Using console LPCM 5.1

1. Set the console's audio output to **5.1 LPCM**.
2. Check that the capture card exposes **48 kHz 6-channel or 8-channel PCM**
   with a supported channel layout to the PC. **5.1 HDMI passthrough support alone is not sufficient.**
3. In the app's Audio tab, select **WASAPI Shared** and enable **Console LPCM 5.1**.
4. For actual 5.1 speaker playback, configure the Windows output device for **5.1 speakers** too.

Six-channel input is passed as 5.1; supported eight-channel input is mixed down
to 5.1. On stereo output devices, the Windows shared audio engine downmixes to
stereo. Dolby/DTS decoding, virtual surround, and ASIO/Exclusive 5.1 output are
not supported. Physical channel-by-channel listening validation is not yet
complete, so check speaker positions with the console's speaker test.
See the [audio guide](docs/AUDIO.md) for channel-volume behavior and supported layouts.

### Listening without video

Enable **Audio-only mode** in the Audio tab to play capture audio without video
capture or presentation. Its dedicated view shows master/L/R volume, output
levels, and clipping status.

- **Scroll** over a master/L/R area to adjust volume; **double-click** to reset that volume to 100%.
- **Drag** inside the view to move it; drag an **edge** to resize at a fixed aspect ratio.
- The chosen window size is remembered for the next launch, and **Hide title bar** also applies.

Console LPCM 5.1 is available in audio-only mode when its requirements are met.
See the [audio-only guide](docs/AUDIO.md) for details.

## Everyday controls

| Control | Action |
| --- | --- |
| `F2` | Reopen settings |
| `Tab` | Show/hide live diagnostics |
| `F3` | Show/hide the audio meter over video (audio-only has its own view) |
| `F5` | Restore source-resolution 1:1 size (video mode) |
| `F6` | Toggle NVIDIA VSR request (video mode) |
| `F1` | App information and shortcuts (F1/Esc closes only the help window) |
| `F12` | Save a source-resolution screenshot (video mode) |
| `F11` | Toggle borderless fullscreen |
| `Esc` | Leave F11 fullscreen; close the app in windowed or automatic-fullscreen mode |
| Mouse wheel over viewer | Change app volume in 5% steps |
| Mouse wheel over an L/R card | Change that channel only (show the meter with `F3` in video mode) |
| Double-click a master/L/R area in the audio-only view | Reset that volume to 100% |
| `Shift` + drag | Temporarily bypass edge snap |

Without VSR, Pixel-perfect maps one video pixel to one display pixel for a sharper image,
but fixes the window size. With it off, the window can be resized freely while
keeping the aspect ratio; choose Smooth or Sharp scaling in settings.
In Pixel-perfect fullscreen, video that fits is centered at its original size;
only video larger than the display is scaled down, keeping its aspect ratio.
With VSR enabled in settings, the control becomes **Lock display size**: it keeps
the selected display resolution, which can differ from the capture resolution.
It does not mean 1:1 source pixels when the two sizes differ; use F5 to restore
source size. F6 does not switch between these size policies during playback.

**Hide title bar** removes windowed-mode borders and is separate from `F11`
fullscreen. **Keep relative window size when moving monitors** maintains similar
screen coverage across monitors. It is independent of Pixel-perfect, so press
`F5` if you need exact 1:1 sizing after a move. F5 also resets the relative-size
baseline to the restored capture-sized window, including when VSR's display size
differs. The window position is saved. Edge snap can be enabled or disabled and
uses the same edge-distance threshold for snapping and releasing; hold Shift to bypass it.

**Since 2.0.1:** **Window → Rounded corners (Windows 11)** uses the same standard
system-drawn radius for regular/borderless video and audio-only windows (default
ON, saved between launches). Regular windows retain their system border; only
borderless windows hide the DWM outline while rounding is enabled.
Maximized and fullscreen windows stay square.
Turn it off to retain every corner pixel. It does not rescale video or add an
app-side mask/render pass; unsupported systems keep their existing appearance.
Windows may suppress rounding in snapped/remote environments; see
[Microsoft's rounding policy](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-rounded-corners).

Enable **Allow volume boost above 100%** in audio settings to raise master volume
up to 200%. Individual L/R volume is capped at 100%; boosting loud input can
cause clipping. **Background auto-mute** silences output while another window
is active without stopping capture. The audio meter's position over video is
also configurable.
When boost is enabled, the audio-only view and F3 meter indicate the 200% maximum;
master values above 100% also use orange in those views and the volume notification.
The clipping warning is separate from this boost indication.

In fullscreen, the cursor hides after two seconds of inactivity and reappears
when you move the mouse or scroll. Choose **Always show** on the **Window**
page if you prefer a visible cursor.

Enable **Start directly next time** to skip the settings window. Hold `Shift`
while launching, or press `F2` from the viewer, to open it again.

Use **Language** on the **App** page to follow the Windows language or explicitly choose
Korean or English. The same page offers automatic update checks after startup
(enabled by default) and manual checks. It does not install updates
automatically; accepting the prompt opens the installer link in your browser.

## NVIDIA VSR (experimental)

**Since 2.0.1:** HDR10/YUY2 + VSR support below is not included in v2.0.0.

At startup, the viewer checks its default D3D11 rendering adapter once, before
showing settings. On Intel/AMD or an unidentified adapter, VSR and its capture-size
selector are disabled; F6 cannot enable it. Identification failure has a separate
message. This vendor check does not certify RTX support or actual enhancement,
does not change GPU selection, and is not repeated on tab changes or per frame.

The **Video** page has an opt-in NVIDIA VSR checkbox and a **Setup guide**
button. VSR defaults to OFF; the checkbox and F6 preference are saved on normal
exit. Supported viewer routes are NV12/YUY2 SDR, MJPEG decoded to NV12, and P010 HDR10
displayed at the source size or larger. Same-size output can request native-resolution
de-artifacting. With VSR enabled in settings, the main resolution selects display
size and **VSR capture** selects the input resolution independently. **Lock display
size** keeps that selected window size; disable it for manual resizing. F6 changes
only the effect, without restarting capture or changing either resolution. F5
restores source-size display. Detected formats/FPS and screenshots follow the input
resolution. YUY2 uses the existing processor directly, with no extra app-side conversion pass.
Downscaling in either dimension bypasses VSR. Capture resolutions
include 1280x720, 1920x1080, 2560x1440 and 3840x2160, subject to device support.

For native HDR, select **P010** and keep the usual HDR10/Windows HDR setup.
The existing P010 → 10-bit BT.2020/PQ output is retained; no SDR intermediate or
RTX Video HDR (SDR-to-HDR conversion) is enabled. Use a recent NVIDIA driver:
[NVIDIA added HDR VSR upscaling in January 2025](https://nvidia.custhelp.com/app/answers/detail/a_id/5448/).
If a driver accepts the VSR request but cannot process the frame, the viewer
turns VSR off and retries that frame once, preserving the HDR route. The request
status reports rejection; ordinary HDR rendering errors are not suppressed.

For example, choose **VSR capture: 1920 × 1080** and **Display resolution: 2560 × 1440**
for 1080p capture in a 1440p-sized window. Choose 1920 × 1080 for both to request
same-size processing. These options configure the viewer/capture path; they do
not change the console's HDMI output mode or the monitor's desktop resolution.

**Tab** shows VSR ON/OFF and input → displayed video size.
ON means the feature is enabled in the viewer, not that driver activation has been detected. Rejected, unavailable,
and failed/unknown states are distinguished. No VSR-added-latency number is shown:
CPU call time is not equivalent to actual added display latency.

**Setup guide** explains how to enable VSR; it does not run a GPU test or claim
compatibility or activation. Enable RTX Video Super Resolution and check its active indicator in
[NVIDIA App / Control Panel](https://nvidia.custhelp.com/app/answers/detail/a_id/5448).
The viewer does not change NVIDIA profiles or add a frame queue/wait to match
ON/OFF latency. VSR processing can increase latency and GPU use. Screenshots
remain source images without VSR.

## Screenshots

Press **F12** during video playback to save a PNG in **Pictures / LowLatencyCaptureViewer**.
The bottom of the **Video** page has **Open screenshot folder** and an opt-in
**Also copy screenshots to clipboard** setting (off by default).

**F1** opens a modeless guide with the app version, keyboard shortcuts, mouse controls,
and screenshot information without stopping playback.
Use F1, Esc, or its Close button to dismiss it; Esc here does not exit the viewer.

- NV12/YUY2 and decoded MJPEG retain the capture resolution and selected SDR color interpretation.
- P010 HDR10 is converted to **SDR sRGB**, both in the PNG and clipboard image.
  This is not an HDR-preserving export. Highlights and wide-gamut colors are compressed;
  appearance will not be identical to HDR playback. Tone mapping uses a fixed 203-nit
  reference, independent of Windows HDR brightness settings. After gamut conversion,
  max-RGB levels through 100 nits retain their linear brightness; a smooth shoulder
  compresses brighter levels instead of dimming the entire range. This fixed export
  curve is not scene-adaptive or an exact reproduction of OBS's tone mapping.
- Captures exclude OSD, window borders, scaling, and the Sharp display filter.
- One request is processed at a time. Source data is copied once into owned memory;
  conversion, clipboard publication, and PNG encoding run on a lower-priority worker.
  Clipboard publication precedes file encoding, but conversion still takes time.
- A busy clipboard does not prevent file saving. Closing the viewer cancels pending work;
  wait for the completion notice if you need the screenshot. Photos already saved are retained.

## Troubleshooting

| Problem | Try this first |
| --- | --- |
| No video | Close OBS/vendor tools, check the capture device and supported resolution, then choose Auto pixel format |
| Monitor briefly loses signal in Low latency mode | The graphics driver may be incompatible with the tearing presentation path. Change Presentation to **VSync**; borderless mode can remain enabled |
| No audio | Select the audio input that belongs to the chosen video device |
| Occasional audio breakup | Check for buffer shortage in Tab diagnostics, then raise the PCM target in 5 ms steps and test again |
| Exclusive shows retry required | Check the device connection and whether another app is using it, then select **Recheck all devices**. Use WASAPI Shared to resume playback without an Exclusive check |
| VSR appears to have no effect | Check NV12/YUY2/MJPEG SDR or P010 HDR10 input, a supported NVIDIA rendering GPU/driver, native-size or larger output, the Tab state, and NVIDIA's active indicator; a successful request alone does not confirm activation |
| Need more evidence | Enable logging in Guide & logs, reproduce the issue, then send the newest `.log` file from **Open log folder** together with screenshots of settings and Tab diagnostics |

Settings and optional logs are stored in `%LOCALAPPDATA%\LowLatencyCaptureViewer`.
The uninstaller can remove this user data on request.

## Compatibility and feature requirements

AVerMedia GC573 is the primary development and test device. Other DirectShow
capture devices are supported, but driver differences mean that every model
cannot be guaranteed.

- Standard support: compatible 48 kHz mono/stereo PCM or 32-bit float input, and progressive NV12/YUY2 video
- Compatibility mode: MJPEG, always listed for manual selection when the device
  exposes it, with automatic decoder-metadata-based range and BT.601/709 handling.
  When colors still differ from another application, **MJPEG color
  interpretation** appears for an explicit MJPEG selection and permits a manual
  matrix/range override.
- HDR10 requires supported P010 input and an HDR-capable display/output path; ASIO requires a compatible installed driver.
- Console LPCM 5.1 requires 48 kHz multichannel PCM capture and WASAPI Shared output.
- Experimental: NVIDIA VSR
- Not supported: H.264/AVC, MPEG-4, automatic device reconnect

### Viewing HDR

Enable HDR on the source, select **P010 10-bit HDR10**, and enable **Windows HDR
on the monitor displaying the app**. Use Immediate or VSync presentation.
An HDR passthrough display does not by itself mean the PC view is also HDR.

- P010 is **HDR-only** in this app. For normal SDR input, choose Auto or NV12/YUY2.
- P010 without color information is **assumed to be PQ/BT.2020 HDR10**. This is not detection of actual HDR content.
- Use **Force HDR10** only when the input really is HDR10 but the device reports incorrect color information.
  Missing information alone does not require it, and it does not convert SDR to HDR.
- **HDR chroma placement** defaults to Auto. Manual Left/Top-left choices are compatibility
  overrides for incorrect placement information; read the [HDR guide](docs/VIDEO.md) first.
- Supported AVerMedia/Elgato devices receive capture-format-aware internal tone-mapping
  requests at startup. This does not guarantee support for every model or actual HDR delivery.

HDR10 requires PQ/BT.2020 Limited input and a supported GPU output path.
HLG, Full-range HDR, HDR through Compatibility (Blt), and app-provided HDR-to-SDR
display conversion are not supported. Tab diagnostics show the current
display's HDR status; actual brightness and colors can vary by capture device
and monitor.

## Learn more

- [2.0.2 release notes](docs/release-notes-v2.0.2.md)
- [2.0.1 release notes](docs/release-notes-v2.0.1.md)
- [2.0.0 release notes](docs/release-notes-v2.0.0.md)
- [Video formats, scaling, and fullscreen](docs/VIDEO.md)
- [Audio modes, buffers, and clock correction](docs/AUDIO.md)
- [Reading diagnostics and logs](docs/DIAGNOSTICS.md)
- [Detailed troubleshooting](docs/TROUBLESHOOTING.md)
- [Build from source](docs/BUILDING.md)

## License

Copyright (C) 2026 seria-aa. Licensed under the
[GNU General Public License v3.0 or later](LICENSE).

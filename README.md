# Low Latency Capture Viewer

> [한국어](README.ko.md) · [Download the latest release](https://github.com/seria-aa/LowLatencyCaptureViewer/releases/latest)

A lightweight Windows viewer for showing HDMI capture-device video and audio
with low latency. It receives video through DirectShow, presents it directly
with D3D11, and sends capture audio to the chosen output device. Stale video
frames are discarded in favor of the latest frame, making it well suited to a
capture-device window used alongside other work or viewed directly.

No FFmpeg, codec pack, or separate Visual C++ Redistributable is required.

## Features at a glance

| Feature | What it offers |
| --- | --- |
| Low-latency video | Latest-frame-first presentation; Immediate, VSync, and Compatibility output |
| Video formats | NV12/YUY2, MJPEG, and experimental P010 HDR10 input |
| Audio outputs | WASAPI Shared/Exclusive, experimental ASIO, and following the Windows default output device |
| Console LPCM 5.1 | Experimental 5.1 playback on supported equipment; WASAPI Shared only |
| Audio-only view | Audio without video, with master/L/R volume, level meters, and clipping status |
| Window controls | 1:1 display, aspect-ratio resizing, fullscreen/borderless, startup monitor selection, and edge snap |
| Convenience and diagnostics | Automatic clock correction, optional 200% volume boost/background mute, diagnostic logs, update checks, Korean/English |

## Download

Get one of the files from the [latest release](https://github.com/seria-aa/LowLatencyCaptureViewer/releases/latest):

- **Setup.exe — recommended:** installs shortcuts and an uninstaller.
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

## What should I choose?

### Video

| Setting | Good starting choice |
| --- | --- |
| Capture resolution | **1920 × 1080**; change it to match the source and capture device |
| Pixel format | **Auto (NV12 preferred)** |
| Frame rate | **Auto**, or the source's actual output rate |
| Presentation | **Immediate (minimum latency)**; **VSync (reduced tearing)** waits for refresh. **Compatibility (Blt + VSync)** uses an alternative output path, may add latency/GPU load, and does not support HDR10 |
| Display monitor | **Auto (restore last position)**; select a monitor to choose the startup location. A missing monitor falls back to the primary display; moving the window afterward is still allowed |
| Pixel-perfect | On for exact 1:1 output; off for a freely resizable window |

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
| Console LPCM 5.1 | **Off (default)**. Experimental, WASAPI Shared only; requires multichannel capture and playback equipment |

WASAPI Shared is the default mode for compatibility with other applications
and Windows effects. ASIO is experimental and appears only when an ASIO driver
is installed. WASAPI Exclusive is available only on output devices that pass
the app's playback-event check. Use WASAPI Shared unless you have a specific
reason to choose another mode.

If sound occasionally breaks up, open the Tab diagnostics overlay. Raise the
PCM target in 5 ms steps from its current value (e.g. `20 → 25 → 30 ms`) only
when **buffer shortage** or **resampler output shortage** repeats.
Leave it alone when there are no errors.

### Using console LPCM 5.1 (experimental)

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
| `F5` | Restore pixel-perfect size |
| `F1` | App information and shortcuts (F1/Esc closes only the help window) |
| `F12` | Save a source-resolution screenshot (video mode) |
| `F11` | Toggle borderless fullscreen |
| `Esc` | Leave F11 fullscreen; close the app in windowed or automatic-fullscreen mode |
| Mouse wheel over viewer | Change app volume in 5% steps |
| Mouse wheel over an L/R card | Change that channel only (show the meter with `F3` in video mode) |
| Double-click a master/L/R area in the audio meter or audio-only view | Reset that volume to 100% |
| `Shift` + drag | Temporarily bypass edge snap |

Pixel-perfect maps one video pixel to one display pixel for a sharper image,
but fixes the window size. With it off, the window can be resized freely while
keeping the aspect ratio; choose Smooth or Sharp scaling in settings.
In Pixel-perfect fullscreen, video that fits is centered at its original size;
only video larger than the display is scaled down, keeping its aspect ratio.

**Hide title bar** removes windowed-mode borders and is separate from `F11`
fullscreen. **Keep relative window size when moving monitors** maintains similar
screen coverage across monitors. It is independent of Pixel-perfect, so press
`F5` if you need exact 1:1 sizing after a move. The window position is saved,
and edge snap can be enabled or disabled.

Enable **Allow volume boost above 100%** in audio settings to raise master volume
up to 200%. Individual L/R volume is capped at 100%; boosting loud input can
cause clipping. **Background auto-mute** silences output while another window
is active without stopping capture. The audio meter's position over video is
also configurable.

In fullscreen, the cursor hides after two seconds of inactivity and reappears
when you move the mouse or scroll. Choose **Always show** in the Video & window
tab if you prefer a visible cursor.

Enable **Start directly next time** to skip the settings window. Hold `Shift`
while launching, or press `F2` from the viewer, to open it again.

Use **Language** in settings to follow the Windows language or explicitly choose
Korean or English. The **Updates** tab offers automatic checks after startup
(enabled by default) and manual checks. It does not install updates
automatically; accepting the prompt opens the installer link in your browser.

## Screenshots

Press **F12** during video playback to save a PNG in **Pictures / LowLatencyCaptureViewer**.
The bottom of the **Video & window** tab has **Open screenshot folder** and an opt-in
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
| Need more evidence | Enable logging in Shortcuts & diagnostics, reproduce the issue, then send the newest `.log` file from **Open log folder** together with screenshots of settings and Tab diagnostics |

Settings and optional logs are stored in `%LOCALAPPDATA%\LowLatencyCaptureViewer`.
The uninstaller can remove this user data on request.

## Compatibility and experimental features

AVerMedia GC573 is the primary development and test device. Other DirectShow
capture devices are supported, but driver differences mean that every model
cannot be guaranteed.

- Standard support: compatible 48 kHz mono/stereo PCM or 32-bit float input, and progressive NV12/YUY2 video
- Compatibility mode: MJPEG, always listed for manual selection when the device
  exposes it, with automatic decoder-metadata-based range and BT.601/709 handling.
  When colors still differ from another application, **MJPEG color
  interpretation** appears for an explicit MJPEG selection and permits a manual
  matrix/range override.
- Experimental: ASIO output, P010 10-bit HDR10, and console LPCM 5.1
- Not supported: H.264/AVC, MPEG-4, automatic device reconnect

### Viewing HDR (experimental)

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

- [Video formats, scaling, and fullscreen](docs/VIDEO.md)
- [Audio modes, buffers, and clock correction](docs/AUDIO.md)
- [Reading diagnostics and logs](docs/DIAGNOSTICS.md)
- [Detailed troubleshooting](docs/TROUBLESHOOTING.md)
- [Build from source](docs/BUILDING.md)

## License

Copyright (C) 2026 seria-aa. Licensed under the
[GNU General Public License v3.0 or later](LICENSE).

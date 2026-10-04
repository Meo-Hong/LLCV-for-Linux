# Rounded viewer corners — development validation

## Regular-window border preservation (2.0.1 preparation)

The outline suppression below is now restricted to borderless windows.
Regular captioned windows always receive DWMWA_COLOR_DEFAULT, while both
restored window modes still use the same DWMWCP_ROUND radius. Frame-mode
changes invalidate the cached border value even if the radius is unchanged.
Fullscreen/maximized remain square; no region, video mask or render pass added.
Tests cover regular/borderless switches, repeated resize caching, video and
audio-only WM_SIZE routing, and native resizable/fixed-caption/popup HWNDs.
Native DWM preference readback is not a screenshot guarantee of identical edge
pixels on every Windows configuration.

## Remove DWM outline (2026-10-04)

The rounded-window option now also sets `DWMWA_BORDER_COLOR` to `DWMWA_COLOR_NONE`;
OFF restores `DWMWA_COLOR_DEFAULT`. This suppresses the DWM outline without changing
the radius, shadow, styles, client size, hit testing or rendering pipeline.
The cache includes border policy, including toggles while the corner preference
stays square in fullscreen/maximized states. Unsupported setters remain nonfatal.

Rebuilt viewer and focused tests: 3/3 passed (0.22 s). Mock tests check the exact
border values and repeated-call caching; native regular/popup HWNDs accept the
setters and retain their client geometry. An initial test attempted border-color
readback, which returned E_INVALIDARG: this attribute is documented for setting,
not getting. The corrected native test checks setter success rather than claiming
border readback. Visible edge pixels have not been screenshot-certified.
[Microsoft border-color API](https://learn.microsoft.com/en-us/windows/win32/api/dwmapi/ne-dwmapi-dwmwindowattribute).

Executable: `outputs/rounded-corners/LowLatencyCaptureViewer_NoOutline_Test.exe`.

## Standard-radius refinement (2026-10-04)

Increased the opt-in preference from `DWMWCP_ROUNDSMALL` to `DWMWCP_ROUND`.
Rebuilt the viewer and related tests; corner API/cache, production window-mode
transitions and geometry passed 3/3 (0.30 s). OFF/maximized/fullscreen remain
`DWMWCP_DONOTROUND`. No settings schema, video path or window dimensions changed.
New executable: `outputs/rounded-corners/LowLatencyCaptureViewer_Round_Test.exe`.
The original small-radius executable remains available for comparison.

2026-10-04. This is an unreleased change, alongside the pending HDR+VSR work.

## Behavior

- Window settings: Rounded corners (Windows 11), default ON, saved as
  `[Window] RoundedCorners`. Video and audio-only viewers share the preference.
- Uses `DWMWCP_ROUND` for restored regular/borderless windows and explicit
  `DWMWCP_DONOTROUND` when OFF, maximized or fullscreen. Restore reapplies the
  standard radius. The user's curvature refinement increased this from the initial
  `DWMWCP_ROUNDSMALL`. Creation and composition/theme changes invalidate cached state.
- Only the viewer's top-level HWND receives the hint. No window regions,
  layered-window conversion, frame-style/client-size changes, video masks,
  render-thread work or extra video queue. Identical resize events do not repeat
  DWM calls. Unsupported DWM calls are nonfatal and cached until a state change.
- The preference is a system hint, not a visual guarantee in every environment.
  Windows can suppress rounding for snapped/remote/unsupported configurations.
  [Microsoft policy](https://learn.microsoft.com/en-us/windows/apps/desktop/modernize/ui/apply-rounded-corners).

## Initial small-radius build checks

- Clean x64 Release build, all private diagnostic build options OFF.
- Targeted CTest: **16/16 passed, 141.93 seconds**, no skips. Settings persistence,
  Korean/English layout, dark/light lifecycle and tab order, native corner API,
  window geometry/moves, audio-only, output transitions, fault/stress, VSR,
  HDR/SDR and overlays covered.
- Native hidden regular and popup windows: 18 successful DWM preference
  readbacks; styles and client dimensions unchanged, no window region added.
  Separate deterministic tests cover maximized/fullscreen/OFF/restore states,
  failed API caching, 1,000 duplicate applies and theme/handle invalidation.
- Production WM_SIZE handler exercised with video/audio-only and bordered/
  borderless combinations; restore/maximize/fullscreen/OFF and stable caching
  checked. These message tests simulate geometry, not a physical display.
- Rendered real settings controls in test fixtures. Inspected Korean dark and
  English light Window page snapshots; new checkbox follows existing alignment
  and spacing. Fixture version text is test data, not the executable version.
- Local Defender custom scan of the test EXE completed with no threats found.
  This does not certify third-party scanners or future signature versions.

Logs/snapshots: `outputs/rounded-corners/`.
Test EXE SHA256: `F7050DD549EBCA83CB6E4770EFA52D64920534CC6EA1ED6B37F8B3DA5B800460`.

Limits: native DWM acknowledgement is not screenshot verification of the visible
viewer's clipped corners. No new physical capture-to-display latency measurement
or long-running live console test. Rendering internals are unchanged by the
corner feature; compositor/driver performance is not certified by these tests.

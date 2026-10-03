# NVIDIA VSR integration and experiment history

## Final v2.0.0 release validation (2026-10-04)

Clean x64 Release build with all three private diagnostic options OFF passed
43/43 CTest cases in 469.74 seconds, including UI/IME lifetime, settings/theme,
VSR, HDR/SDR, screenshot, audio replay and update-discovery coverage. No tests
were skipped. Earlier failures below are retained as chronological experiment
records, not the final suite result. See tests/RELEASE_V2_0_0_AUDIT.md for scope
and limitations; no new physical end-to-end latency certification is implied.

Version 2.0.0 integration: ordinary builds include an
opt-in VSR checkbox, F6 toggle and a setup guide in the
Video settings page. Default OFF leaves the original processing untouched.
The normal build saves the preference and has no per-frame VSR timing
instrumentation. No NVIDIA SDK, driver installation, global/profile settings
changes or extra DLLs are needed.

Native-resolution processing is now included in the opt-in policy: both output
dimensions must be at least the input dimensions. A same-size frame uses the
same extension request and VideoProcessorBlt as an upscaled frame, without
additional surfaces, frame queues or readback. Either-axis downscaling remains
bypassed. The driver can still reject or ignore a request; eligibility is not
an activation guarantee. Existing native benchmark commands now exercise this
production policy directly, without test-only eligibility overrides.

`LLCV_VSR_EXPERIMENT=ON` is now only the private executable/diagnostics variant:
it suppresses settings migration/saves and update checks, supports the old CLI,
and enables CPU timing. Earlier feature-OFF test descriptions below refer to the
pre-integration implementation, not the current build's VSR availability.

The setup guide only explains how to enable VSR and use NVIDIA's active indicator.
It does not create a processor or claim compatibility or activation. The old
request probe remains available to developer tests only; historical probe results
below are not evidence of actual activation.

## Scope

### Settings refinement (2026-10-03)

- Replaced the user-facing request probe with a setup guide. Opening it does
  not create a video processor, query support or report an activation result.
  The developer-only `--probe` diagnostic remains separate.
- Refined the existing 950 x 650-DIP settings shell: neutral graphite/mint
  palette, 10-point body and section text, 9-point secondary text, 20-point page
  titles, consistent 30-DIP closed combo fields and 24-DIP drop-down rows.
  Conditional Window guidance no longer leaves an empty reserved gap.
- Shortened bilingual captions and grouped VSR/screenshots without removing the
  detected format/FPS summary or HDR/5.1 options. Native Tab order follows the
  visual groups; labels remain directly before their fields for accessible names.
- Design references: [Linear's hierarchy and quieter chrome](https://linear.app/now/how-we-redesigned-the-linear-ui),
  [Raycast settings](https://manual.raycast.com/settings), and
  [Nova's purpose-based settings](https://help.nova.app/settings/).
  This pass changes settings UI only, not the playback pipeline or frame pacing.

Validation and limitations:

- Release build with private VSR instrumentation OFF succeeded.
- Layout tests passed 24 factory profiles and 1,200 combinations with Korean/
  English at 96/144/192 DPI; conditional spacing, font retirement, native field
  heights and text fit are checked. Native keyboard tests passed 102 profiles.
- The full sweep passed 39/40 tests (`outputs/settings-refined/ctest.log`).
  The pre-existing, separately linked F1 `ViewerHelpTests` resource assertion
  failed again; its cause is not resolved by this settings work.
- Final focused settings/VSR runs passed layout, controller, settings persistence
  and VSR tests, but the whole-process `SettingsThemeTests` resource assertion
  failed twice (GDI 18 to 55, USER 2 to 36). These failures remain recorded in
  `ui-final-ctest.log` and `ui-isolated-ctest.log`; they are not an all-green claim.
- An instrumented follow-up (`theme-resource-diagnostic.log`) passed all 32
  measured cycles with GDI 55 to 55 and USER 36 to 35. The earlier increase
  coincided with Windows text-input/IME windows being initialized. No test/app
  widgets remained after destruction. This supports delayed native initialization
  as a hypothesis, but timing-sensitive diagnostics do not prove the default
  failure fixed. The original zero-growth assertions and cycle counts remain.
  Use `SettingsThemeTests --trace-resources` to collect this evidence again.
- Native interactive preview verified setup-guide opening, keyboard navigation,
  page layout and dismissal without opening capture hardware. Snapshot device
  labels are fixture data, not capability or VSR activation evidence.

Preview (not a release):
`outputs/settings-refined/LowLatencyCaptureViewer_UI_Refined_Preview.exe`.
`Open-Settings.cmd` opens it with `--force-settings`. The previously running
viewer was not stopped or overwritten.

### Integrated preview validation (2026-10-02)

- Production-path Release build with private diagnostics OFF succeeded.
  CTest: **37/38 passed** in 292.17 seconds. VSR default/OFF pixel identity,
  six live toggle pairs, settings save/load, bilingual UI captions/layout at
  96/144/192 DPI, HDR/SDR, screenshots, audio replay and transition tests passed.
- ViewerHelpTests still fails its repeated-lifecycle GDI/USER resource assertion.
  The preserved pre-VSR release test binary fails the same assertion now.
  Root cause remains unresolved; this is not an all-green release certification.
- Explicit support probe ran three times on RTX 3080: request accepted each time,
  viewer intent unchanged. Global NVIDIA setting/actual activation not verified.
- Delivered preview: `outputs/vsr-integrated/LowLatencyCaptureViewer_VSR_Preview.exe`,
  SHA256 `9CC3F21769308CF2218A37F1A73F969213A418918299AA2A6E04ACD9E217C3F2`.
  This normal-settings preview persists the choice, unlike earlier private trials.
- Logs: `outputs/vsr-integrated-regression.log`,
  `outputs/vsr-integrated-probe.log`, `outputs/vsr-integrated-help-baseline.log`.

### Private diagnostic variant

- `LowLatencyCaptureViewer_VSR_Test.exe --vsr-test off --force-settings`: start
  with the VSR checkbox OFF.
- `LowLatencyCaptureViewer_VSR_Test.exe --vsr-test on --force-settings`: request
  NVIDIA VSR with identical CPU diagnostics.
- No argument: use the saved preference (OFF when missing).
- **F6** in the viewer switches ON/OFF within the same renderer, starting ON
  from a default launch. Key auto-repeat is ignored. The UI only publishes an
  atomic request; the render thread applies it before the next video frame.
  No capture/audio restart, swapchain rebuild or new frame queue is involved.
  A brief HUD distinguishes pending, ON requested, OFF, unavailable and failure.
  It never asserts that the driver actually activated a specific VSR model.
- Only NVIDIA + NV12 SDR + native-size or upscaled video is eligible.
  Decoded MJPEG can also enter the NV12 path. P010/HDR, YUY2, audio-only,
  non-NVIDIA and video downscaled in either dimension remain untouched.
- Same existing VideoProcessorBlt, upload ring, Present policy and latest-frame
  capture slot. No extra GPU surfaces, CPU readback, buffering or waits in the
  viewer. No quality override; quality/activation depend on NVIDIA settings.
- The driver accepting a request is logged as **requested**, never **active**.
  Rejected enable requests fall back to an explicit OFF request. If even OFF
  fails, initialization fails rather than produce an ambiguous A/B baseline.
- Experimental executable does not migrate/save user settings. F2 relaunch
  preserves the on/off argument. Trial sessions enable logging so F6 comparisons
  are captured; logs summarize each segment at toggle/resize/recovery/exit,
  not every frame. CPU timing warmup restarts after each toggle.
  Automatic update checks are disabled in this private executable.
- Screenshot export remains the original capture frame, without VSR.

## Interactive comparison

Close other viewers before opening the same capture card. Use the same source,
GPU driver quality, capture dimensions/FPS, window dimensions, display and
presentation mode for both runs. Use NV12 and Smooth scaling to avoid stacking
Sharp with VSR; for upscaling, turn off Pixel-perfect and enlarge the window
beyond the input dimensions. Same-size output can request native-resolution
de-artifacting instead, including when Pixel-perfect is enabled.
Do not silently force a lower capture resolution or change the display mode.

Use `VSR-Compare.cmd` (starts OFF with settings), or launch the EXE directly.
Press F6 for OFF/ON/OFF, ideally at least 30 seconds each for timing logs,
including motion. Quick toggles are useful for visual comparison but may leave
no samples after the five-second warmup. Verify NVIDIA's own
video enhancement status if available; an accepted extension request alone does
not prove the intended model/quality is active. Close each viewer to flush the
summary. Both runs exclude the first five seconds' worth of configured frames.

CPU VideoProcessorBlt call duration and capture-callback-to-Present-return are
diagnostic metrics, **not GPU completion or input-to-display latency**. Actual
display latency needs a displayed-frame timing trace or external measurement;
constant FPS does not demonstrate constant latency.

## Synthetic completion test

`VsrExperimentTests --bench` uses the actual app renderer on a hidden window,
moving synthetic NV12, 1920x1080 -> 3840x2160, 60 fps pacing, Smooth scaling.
It runs OFF/ON three times, 240 frames/run, excluding the first 60.
No capture/audio devices, visible windows or user settings are opened.

Only this standalone test inserts a one-pixel staging readback after each blit
and waits for completion. Its elapsed time includes upload, video processing,
readback, driver/CPU overhead, and deliberately serializes work. It is a
cross-check of processing cost, **not an estimate of actual display latency**.
Large outliers may also reflect scheduling/power state, not just VSR.

An initial RTX 3080 run with D3D11 timestamps around VideoProcessorBlt reported
roughly 0.005 ms OFF and 0.04-0.05 ms ON, while the independent forced-completion
test showed several milliseconds of extra cost. Those GPU timestamps are not
used in the final implementation: they substantially under-reported this path.
Neither CPU API return time nor these timestamps should be advertised as VSR
latency. Original intermediate logs are diagnostic evidence, not final results.

### Local observations (2026-10-02)

GPU: NVIDIA GeForce RTX 3080, driver video quality not changed or independently
verified. Final test has no GPU timestamp queries. Each row is a separate
OFF/ON pair with 180 measured moving frames per run:

| Pair | OFF mean | ON mean | Difference |
|---|---:|---:|---:|
| 1 | 1.9618 ms | 7.3892 ms | +5.4274 ms |
| 2 | 1.4117 ms | 7.4383 ms | +6.0266 ms |
| 3 | 1.2987 ms | 7.9338 ms | +6.6351 ms |

These are serialized **upload + VP + staging readback** timings, not just model
inference and not actual interactive display latency. They justify testing an
opt-in feature, not declaring its latency acceptable. Local logs:
`outputs/vsr-bench-final.log`; earlier cross-check:
`outputs/vsr-bench-moving.log` (different instrumentation).

Final output hashes were consistent across all three OFF runs and, separately,
all three ON runs, with different OFF/ON pixels. This demonstrates that the
request affected output on this machine, not which exact driver model or quality
level ran. Default/no-request and explicit OFF matched byte-for-byte in the
separate recreation guard test. No real capture-to-display/HDMI timing was run.

### Build and regression status

- Experiment ON: Release build succeeded; new VSR guard test passed, including
  identical default/OFF pixel hashes and renderer recreation.
- Experiment OFF: separate Release app build succeeded; its build graph contains
  no VSR source or feature definition.
- Full experimental build suite: **37/38 passed**, including HDR/SDR GPU tests,
  screenshot tests, video lifecycle and shared/surround replay tests.
- `ViewerHelpTests` failed its repeated-help GDI/USER object assertion, including
  a standalone rerun. The preserved pre-change executable at
  `build-release-readiness/ViewerHelpTests.exe` failed the same assertion in this
  environment. Cause is unresolved; this is not an all-green release audit.
- No public release, commit/push, driver settings changes or capture-device
  tests were performed for this experiment.
- Pre-F6 trial executable SHA256 (superseded):
  `910C8CB1B2859CC3D1BE51A281E98432280309F6953974D123C449B483FB7002`.

### Live F6 follow-up validation

- Six live ON/OFF pairs on RTX 3080 changed ON pixels and restored the exact
  baseline hash on every OFF. Device, processor, swapchain and output generation
  stayed unchanged throughout the toggles.
- Covered auto-repeat, audio-only ignoring F6, last-request coalescing, next-frame
  application, occlusion, 1:1 bypass and reapplying ON after an actual resize.
- All six VSR HUD messages fit the existing 228x62 text box in Korean and English.
- An initial startup hint unnecessarily enabled HDR overlay scratch work; removed
  it so the default/no-request UI-free path remains unchanged. The HDR regression
  test caught this and passed after repair.
- Targeted suite: **6/6 passed** (13.35 seconds): VSR, audio callbacks, HDR GPU,
  SDR GPU, monitor moves and settings. Separate feature-OFF app build passed.
  This is not a fresh all-green full-suite audit; the earlier help-test issue
  remains separately unresolved. No real capture-to-display latency measurement.
- Pre-720p F6 trial executable SHA256 (superseded):
  `66C8991E81853BFF6F552E2CF4681954A336E167AD266B6D4D5629E27B359430`.

### 720p capture preset follow-up

- Added 1280x720 to the capture resolution list, with a 60 fps automatic target.
  Manual FPS and driver format negotiation are unchanged. Existing enum values,
  default 1080p and invalid-runtime-preset fallback to 1080p are preserved.
- Settings tests cover all four resolutions with Auto/60/120 fps save/load;
  bilingual native control tests cover selection of all four entries.
- Targeted regressions: **7/7 passed** (61.50 seconds), covering settings UI/store,
  VSR, HDR/SDR pipelines, monitor moves and DirectShow format handling.
- `VsrExperimentTests --720p` passed on RTX 3080 with synthetic 1280x720 input
  and initial 1920x1080 output, including six live A/B pairs and exact OFF hash
  restoration. Feature-OFF Release app build also passed.
- This is not a physical 720p capture-device or end-to-end latency test, nor a
  new full-suite audit. Trial settings remain intentionally non-persistent.
- Delivered EXE SHA256:
  `7C0B97E0F19874D878348879AA72B7D8FD5D53D9EE8351D7306466B2EFF9D95F`.

## Additional measurements

### 720p to 1440p processing-cost measurement (2026-10-02)

- RTX 3080, driver 616.92; moving synthetic NV12 1280x720 -> 2560x1440,
  Smooth, 60 fps pacing. No viewer/capture app process was found before testing.
- Ran `VsrExperimentTests --bench-720p` and then
  `VsrExperimentTests --bench-720p --reverse`: six OFF/ON pairs total.
  Each phase: 360 frames, first 120 excluded, 240 measured (1,440 per state).
  Actual output dimensions and accepted ON/OFF requests are asserted.
- Mean milliseconds per pair (OFF / ON / difference):
  2.6626 / 5.9299 / 3.2673;
  1.7665 / 5.8887 / 4.1222;
  1.9480 / 6.1273 / 4.1793;
  1.7512 / 6.0831 / 4.3319;
  1.9393 / 5.7587 / 3.8194;
  1.7743 / 6.0645 / 4.2902.
- OFF output hash was consistently 85CFCDACCB91B1E2; ON was consistently
  53E75ECC8BD01459. Driver quality/model/actual RTX activation was not
  independently verified or changed.
- This measures serialized upload + video processing + one-pixel readback and
  its CPU/driver overhead, NOT pure AI inference or capture-to-display latency.
  No Present, physical capture, display scanout or input latency was measured.
  The test-only blocking readback is never added to the viewer.
- Logs: `outputs/vsr-bench-720-1440-forward.log` and
  `outputs/vsr-bench-720-1440-reverse.log`. Only the benchmark was rebuilt;
  the delivered 720p viewer executable was unchanged.

### 720p to 4K follow-up (2026-10-02)

- Same RTX 3080 / driver 616.92 / synthetic moving NV12 / Smooth / 60 fps
  methodology as the 1440p test, now asserting actual 3840x2160 output.
  No viewer process was found before testing; no driver quality changes made.
- Commands: `VsrExperimentTests --bench-720p-4k`, then the same with
  `--reverse`. Six pairs, 240 measured frames per phase after 120 warmup frames,
  1,440 measured frames per state.
- Mean milliseconds per pair (OFF / ON):
  2.1072 / 6.2376; 2.1851 / 6.0160; 2.1362 / 6.1151;
  2.0525 / 6.0913; 2.1967 / 6.0072; 2.0212 / 6.2432.
- OFF hash AE3A67F7F06D9305 and ON hash E258E15A911F0AC9 were consistent
  across all phases. Request accepted/pixel changes are not independent
  confirmation of RTX quality/model activation.
- All previous caveats apply: serialized upload + VP + readback completion,
  not pure inference or physical input-to-display latency. No Present or HDMI
  measurement. Viewer executable and rendering path were not changed.
- Logs: `outputs/vsr-bench-720-4k-forward.log`,
  `outputs/vsr-bench-720-4k-reverse.log`.

### Native 1080p verification (2026-10-03)

- RTX 3080 / driver 616.92. At the time of this verification production
  eligibility still bypassed 1:1. Only the test instance overrode that gate
  after asserting the production bypass; no viewer code, settings, capture
  device or NVIDIA profile changed during those measurements.
- `--bench-native` and `--bench-native-color`, each forward and `--reverse`:
  synthetic moving NV12 1920x1080 -> 1920x1080, Smooth, 60 fps pacing,
  six phases each, 360 frames/phase, first 120 excluded. Twelve OFF/ON pairs,
  2,880 measured frames per state overall. No Present.
- Mean serialized upload + VP + forced readback completion (milliseconds):

  | Pattern | OFF | ON | Difference |
  | --- | ---: | ---: | ---: |
  | Neutral chroma | 2.4663 | 5.2325 | 2.7662 |
  | Colored blocks | 2.4032 | 5.3756 | 2.9724 |
  | Combined | 2.4348 | 5.3041 | 2.8693 |

- Neutral OFF/ON hashes: DA7FEEECE25C6D51 / EB915055108CDB88;
  color OFF/ON: F73A912E23A3A2FF / AD22D4957D222352.
  Every repeated state and reversed run gave the same respective pixels.
  Final-frame RGB differences: neutral 89.0378% of pixels, MAE 1.529441/255;
  color 95.7227%, MAE 2.052784/255. These are differences, not quality scores.
- Both color runs additionally passed three same-resource ON/OFF pairs each:
  explicit OFF exactly restored untouched-default pixels, without rebuilding
  the device, processor or swap chain. Existing `--720p` regression passed
  (policy, default/OFF, six live pairs, shortcuts, HUD, occlusion, resize).
- This confirms a repeatable native-size output effect in this extension path
  on this machine. It does not independently identify the RTX model/quality
  setting, establish perceptual improvement, or measure HDMI/display latency.
  Test-only blocking readback is not part of the viewer. These measurements
  preceded the separate decision to enable native-size production eligibility;
  the current benchmark no longer overrides the policy.
- Logs: `outputs/vsr-native-{forward,reverse}.log`,
  `outputs/vsr-native-color-{forward,reverse}.log`,
  `outputs/vsr-native-regression.log` and `outputs/vsr-native-build.log`.
- NVIDIA's VSR 1.5 announcement explicitly describes native-resolution
  de-artifacting, including 1080p; this is background support, not a substitute
  for the measurements above:
  https://blogs.nvidia.com/blog/rtx-video-super-resolution-ai-obs-broadcast/

### Native-size integration and settings UI (2026-10-03)

- Production eligibility now permits equal input/output dimensions, including
  the centered 1:1 video rectangle in Pixel-perfect fullscreen. Downscaling on
  either axis, non-NVIDIA rendering, P010/HDR and YUY2 still bypass this path.
- The GPU regression uses the production gate (no eligibility override), checks
  native-size ON/OFF pixel differences, and verifies that OFF restores the
  untouched-default output on the same processor and textures. Synthetic GPU
  readback remains test-only; there is no additional viewer frame queue, copy,
  timing query or synchronization wait from this integration.
- Settings use a compact five-page sidebar (Video, Audio, Window,
  Help/Diagnostics, App), the audio-only screen's dark palette and mint accent,
  native controls with paint-only decoration, and system colors in high contrast.
  Capture capabilities and per-format frame rates remain visible. Capture and
  display resolution have not been split into separate controls in this build.
- Layout tests cover Korean/English, DPI variation and conditional controls.
  Controller tests exercise production control IDs, disabled/invalid Start
  guards, Exclusive verification gates, sidebar navigation, audio-only mode,
  Enter on Cancel and Escape closing a dropdown before dismissing settings.
  The preview tool opens the real control factory with mock devices and never
  loads/saves user settings or starts capture/audio hardware.
- Native GUI input checks confirmed page switching by mouse/keyboard and
  dropdown dismissal. High-contrast fallback was reviewed, but not exercised by
  changing the user's Windows high-contrast setting. Hardware/driver
  compatibility and subjective picture quality still require user testing.
- Test-harness findings: hidden native button interactions sometimes wait about
  two seconds per lifecycle on this host. All 32 theme cycles complete in about
  70 seconds with stable GDI/USER counts; the CTest limit is 120 seconds, not a
  reduction in coverage. Native cleanup messages are drained between fixtures.
- The pre-existing F1 help resource-count assertion remains intermittent in
  additional standalone repeats (two passes, one failure). Test-only changes
  avoid starting the PNG encoder unnecessarily, drain native cleanup messages
  and verify that no help HWND remains. The original resource assertion is not
  relaxed; a stable verbose 160-lifecycle trajectory does not prove that the
  intermittent failure is harmless. This remains an investigation item, not a
  claim of a leak-free release.
- Final integrated Release build completed successfully. The final CTest run
  passed 40/40 tests (`outputs/vsr-modern-final-ctest.log`, 194.15 seconds with
  two workers), including the unchanged strict F1 assertion in that run. This
  does not supersede the separate intermittent-repeat finding above. Final
  bilingual/DPI snapshots and theme lifecycle checks also passed. Test EXE:
  `outputs/vsr-modern/LowLatencyCaptureViewer_VSR_Modern_Preview.exe`, SHA-256
  `A9A6385501166BBFFD8529742744EFBF11E7D4BB990BD9C06FDF746D9A753079`.

## Complete capture/display settings preview (2026-10-03)

- Added separately persisted VSR capture resolution. Existing profiles migrate
  to their former capture size; invalid values fall back to the main resolution.
- With VSR enabled in settings, the existing resolution field selects initial
  display dimensions. Format/FPS discovery uses the VSR capture size. Without
  VSR, the existing capture-resolution behavior remains unchanged.
- Capture size is latched before the graph starts. F6 changes only the processing
  request. No capture reconnection, additional queue, or latency equalization
  was added. This is not a new end-to-end latency measurement.
- Display-size locking does not force source-size letterboxing for split output.
  F5, input titles/diagnostics, FPS defaults and screenshots follow capture size.
  Monitor-relative sizing remains available after the selected initial size.
- The native VSR card contains the missing capture selector. Both languages,
  keyboard order and existing sections are retained. Setup guide still does not
  claim NVIDIA activation or compatibility.
- Added 16 capture/display policy combinations with 20 F6 toggles each, real
  D3D11 split-output checks at 720p to 1440p/4K with stable swap chains, independent
  persistence/migration tests and native selector coverage.
- Full clean build used after changing shared settings layout. Explicit build
  dependencies prevent stale AppSettings ABI objects with localized MSVC scans.
- Results: layout/store/VSR tests pass, including 1,200 UI states and 102 native
  tab-order profiles. Korean/English native screenshots inspected. The separate
  F1 help lifecycle resource-baseline failure recurred in the full sweep, then
  passed a focused rerun. The intermittent cause remains unresolved. The final
  theme run passed 32 cycles (GDI 55 to 55; USER 37 to 36). See outputs/vsr-complete
  logs; focused passes do not prove the intermittent issue resolved.
- Preview SHA-256: 43FA91613953B2ADD9C7D79C1FB47C6D6F8024E4F385F3C080C749F88032F499.

## Settings typography and interaction polish (2026-10-03)

- Embedded Pretendard 1.3.9 Regular/SemiBold with SIL OFL 1.1 notices. Fonts are
  private to the process; there is no system installation or download at runtime.
  Native font-selection tests confirm the intended faces, not fallback fonts.
- Increased settings width from 950 to 1000 DIPs. Aligned the two field columns,
  regularized audio option spacing, and retained the VSR capture selector.
- Unchanged text, visibility and geometry are no longer re-applied. Parent
  painting clips native child controls and avoids an intermediate erase pass.
  Tab selection no longer reruns the full layout.
- Capability-dependent controls receive their final enabled state in one pass,
  rather than being enabled then disabled on every refresh. A native message
  counter verifies 100 repeated view updates produce no enable/disable churn.
- Capability discovery uses a dialog-local cache keyed by device and actual
  capture dimensions. Display-only changes do not re-open the capture device.
  New queries run on one background worker, coalescing rapid selections. Video
  and internal-audio probes serialize device access. Results from invalidated
  generations cannot replace the current selection.
- A manual Refresh button invalidates capability results. Query failures also
  remain cached until refresh or a new settings session. Start is disabled while
  the selected video mode is pending. Closing waits for an active driver query
  before capture starts; cancelling an arbitrary blocked driver call is not
  supported.
- Cache tests cover blocked queries, pending coalescing, cached revisits,
  invalidation during work and shutdown. Production-controller tests simulate a
  slow driver and 50 display changes without rescans. Layout tests cover 1,200
  states and bilingual/DPI font selection; native snapshots were inspected.
- Focused settings, persistence, VSR, SDR/HDR pipeline and monitor regression
  tests are recorded in outputs/settings-polish. This changes no per-frame
  video/audio processing and is not a new end-to-end latency measurement.
- The separate, previously recorded F1 help resource-baseline failure recurred
  in the broader run (9/10 tests passed). It remains unresolved and is not
  hidden by the settings-focused pass. This is a preview, not a release.
- Final settings-focused run: 9/9 passed in 110.33 seconds, including native
  lifecycle checks, steady enable-state regression and real D3D11 pipeline tests
  (outputs/settings-polish/smooth-tests.log).
- Preview EXE SHA-256:
  35D1D4519C9E9C0E7FF296BD407DCF8D2FFE420DF718E9DF6DDA02EA01341C40.

## Settings alignment and transitions (2026-10-03)

- Both settings columns now use 376-DIP fields with a 40-DIP gutter. The
  resolution/FPS row is split evenly. Inline VSR labels, refresh action and help
  buttons are vertically centered against their associated labels/controls.
- Window sizing advice has a reserved note area. Enabling the conditional note
  no longer moves the borderless, snapping and cursor controls. Visibility-only
  updates no longer resize the cursor combo or change its Z order.
- Short synchronous page/DPI updates suspend parent redraw and queue one final
  repaint. Hidden and nested update scopes preserve the prior visibility state.
  Same-page selection still refreshes capability gates without a full repaint.
- Custom settings painting uses Windows buffered painting with balanced
  thread-local initialization and a direct-paint fallback. Repeated native
  button/focus/key notifications invalidate once instead of forcing immediate
  painting from inside each notification. Hidden/disabled controls clear hover.
  The video viewer swap chain, capture path and audio processing are unchanged.
- Native regression covers 50 page transitions, nested/hidden update scopes,
  50 button changes coalesced into one paint, 136 bilingual/DPI tab-order profiles
  and 1,600 layout states. Added 125% DPI alongside 100/150/200%, including caption
  bounds and inline vertical-center checks. The 125% check found a too-short
  two-line screenshot description; its height was corrected before packaging.
- Native mouse/keyboard navigation, dropdown opening and Escape dismissal were
  exercised with Computer Use in the no-hardware preview using production UI
  controls. Hardware capture was not started. See outputs/settings-interaction.
- References for UI-only repaint lifetimes:
  https://learn.microsoft.com/en-us/windows/win32/gdi/wm-setredraw
  https://learn.microsoft.com/en-us/windows/win32/api/uxtheme/nf-uxtheme-beginbufferedpaint
- The separately tracked intermittent F1 resource-baseline issue is unchanged.
  This remains a test build, not a public release or a latency measurement.
- Final aligned build: 9/9 focused regressions passed in 105.41 seconds
  (outputs/settings-interaction/aligned-tests.log). Native transition assertions
  also passed separately in transition-tests.log. Packaged EXE SHA-256:
  DAFC7108D3786FFAA530ADADF487450B92F3CD67A5DA819E0F0B6853779AA6E0.

### Settings dark/light themes (2026-10-03)

- App preferences now includes a bilingual Settings theme picker beside Language.
  Dark remains the default for old profiles. Light uses neutral bright surfaces,
  dark text and a dark primary action. The choice previews immediately, persists
  with Start, and is discarded by Cancel, consistent with other dialog edits.
- General/SettingsTheme stores Dark or Light; missing/unknown values use Dark.
  Native combo creation restores the persisted choice. Windows high contrast
  still takes precedence. Theme changes only repaint the settings window:
  no device enumeration, capture reconnect or video/audio processing changes.
- Palettes and brushes are per window. Replacing palette/brushes is transactional
  on allocation failure. Repeated switches preserve native selection/focus and
  reuse the existing subclass/queued repaint mechanism.
- Focused regression: 9/9 passed in 102.60 seconds, including 32 lifecycle cycles
  with repeated dark/light changes, brush/text contrast, settings persistence,
  keyboard order, layout and VSR/HDR/SDR tests. Both languages and 100/125/150/200%
  native snapshots were generated; light App settings and 125% Video inspected.
  This is not a hardware latency benchmark. The earlier F1 intermittent resource
  assertion is unchanged and outside this focused run.
- Preview: outputs/settings-themes/LowLatencyCaptureViewer_Settings_Themes_Preview.zip.
  EXE SHA256: 936BA4000E7FC2D53E20F6571DDA1C9B85142F07652DEB43FF551310097A3DB1.

### Settings readability refinement (2026-10-03)

- Light surfaces are now neutral gray; action, check and focus colors use a
  stronger blue. Main text remains slate and secondary text is darker (72,88,106).
  Dark colors and disabled-state colors are unchanged.
- Embedded Pretendard Medium 1.3.9 is loaded alongside Regular and SemiBold using
  private resource 9004 (9003 remains the license). Labels, values, navigation and
  ordinary buttons use true Medium/500. Small copy stays Regular; headings and
  the primary action retain SemiBold. Font sizes and layout geometry are unchanged.
- SettingsViewTests, SettingsThemeTests and SettingsControllerTests passed 3/3
  in 89.56 seconds, including actual font-face/weight selection and bilingual
  DPI/layout checks. Light Korean 150% and dark Korean 100% renders inspected.
  No video/audio changes or hardware latency claims in this adjustment.
- Preview: outputs/settings-medium/LowLatencyCaptureViewer_Settings_Medium_Preview.zip.
  EXE SHA256: 49E7C05BDF4641C5D19B1F70884DCA12590B453806185F8D837F48633055F8BD.

### Settings text rasterization diagnosis and correction (2026-10-03)

- Confirmed a real format/rendering mismatch, rather than assuming small font
  sizes were the cause: embedded Pretendard OTF used CFF outlines while settings
  requested GDI ClearType. Microsoft documents that GDI ClearType is unavailable
  for PostScript OpenType without TrueType outlines:
  https://learn.microsoft.com/en-us/windows/win32/gdi/cleartype-antialiasing
- Controlled native probe (outputs/settings-text-rendering/FontRasterProbe.cpp):
  identical Medium family, 10 pt, black text/white 32-bit memory DC and ClearType
  flag; only upstream file format differs. CFF had zero subpixel-colored pixels
  at 96/120/144/192 DPI. TTF had 1560/2149/2335/3326 respectively. Both selected the
  intended family; GetFontData verified CFF vs glyf tables. Logs/BMPs/PNGs retained.
  Pixel counts prove rasterizer behavior on this host, not universal readability.
- Replaced all three embedded faces with upstream 1.3.9 alternative TTFs; same
  family, font sizes, weights, geometry and colors. Old OTFs moved to
  outputs/settings-text-rendering/cff-reference for reproducible comparison.
  Source checksums and dependency notice updated. No OS smoothing setting edits,
  font installation, DirectWrite rewrite or video/audio path changes.
- SettingsViewTests now checks actual selected glyph-table format and
  CLEARTYPE_QUALITY for every role/DPI, not only family name and weight. This
  detects the original problem even though the two formats share family names.
- Related suite passed 9/9 in 104.76 seconds. Native snapshots were then hardened
  with GdiFlush before CPU reads of DIB pixels; rebuilt fixture passed 136 keyboard
  profiles plus 50 page transitions, and generated both themes/languages at
  100/125/150/200%. Light 100/150% and dark 100% inspected. The application binary
  did not change during this final test-fixture-only adjustment.
- Preview: outputs/settings-text-rendering/LowLatencyCaptureViewer_Settings_TextFix_Preview.zip.
  EXE SHA256: 2910A68F57872B73CB2E7EEA26810EDA585E97C653680A5B198C2643AD290028.
  Subjective confirmation on the user's monitor is still needed; no hardware
  latency benchmark or release-readiness claim is made.

### Disabled settings-label rendering correction (2026-10-03)

- Pixel-perfect correctly disables scaling, but the native disabled STATIC label
  used embossed highlight/shadow text instead of the theme's flat gray. This is
  separate from the earlier CFF/TrueType font issue. A same-font native baseline
  differed from a plain DrawText reference by 583 pixels at dark/96 DPI.
- Theme painting now handles only disabled no-wrap text labels, with the existing
  font, disabled foreground and matching page/card background. Enabled labels and
  high-contrast mode retain native painting; actual disabled state, keyboard
  behavior and scaling policy are unchanged. The VSR capture label is covered too.
- Exact RGB regression comparisons against an independent plain-text reference
  passed with zero differences for both labels, both themes, 96/120/144/192 DPI
  and two consecutive paints. Re-enabling Pixel-perfect/VSR-dependent controls
  is also checked. Native Pixel-perfect snapshots were generated in both languages
  and four scales; dark Korean 150% and light Korean 100% were visually inspected.
- SettingsViewTests, SettingsThemeTests and SettingsControllerTests passed 3/3
  in 93.72 seconds. Build and git diff --check passed (existing compiler name-
  shadowing warnings and Git line-ending notices remain). No capture hardware or
  video/audio path changes; the previously tracked F1 issue is not addressed here.
- Preview: outputs/settings-disabled-label/LowLatencyCaptureViewer_Settings_DisabledLabel_Preview.zip.
  EXE SHA256: A3BB302A70E2FD90D5C6F86E3CEB3CB2C59CB08D37641C78BA2EBC690D0A9739.
  Logs and native snapshots are retained under outputs/settings-disabled-label.

### Dropdown text alignment and screenshot spacing (2026-10-03)

- All 20 settings dropdowns now use CBS_OWNERDRAWFIXED with CBS_HASSTRINGS.
  Rows retain the existing 24-DIP hit area but center text vertically using the
  current UI font; native item strings/data, keyboard handling and scrolling
  remain with the combo. Selected rows retain Windows highlight colors and
  high-contrast colors are respected. No custom input/list implementation.
  API reference: https://learn.microsoft.com/en-us/windows/win32/controls/create-an-owner-drawn-combo-box
- Narrowly tightened the VSR group's vertical spacing and moved the screenshot
  group up eight DIP. Its card-to-footer gap is now eight DIP, equal to the gap
  between the two cards. Dialog size, font sizes and interactive targets stay
  unchanged. Fractional-DPI geometry assertions verify these gaps.
- Dropdown raster tests compare 64 theme/language/DPI/state combinations against
  an independent font-height-centered reference, all with zero differing RGB
  pixels. Includes selected, unselected, disabled and collapsed-field rendering,
  literal ampersands, native string lookup, item-data retention and arrow keys.
  Both themes/languages and four DPI scales were rendered; light Korean 150%
  and dark English 125% pages and dropdown row strips were visually inspected.
- Logs, fixtures and the preview package are in outputs/settings-dropdown-spacing.
  Focused settings/VSR/HDR/SDR/monitor regression passed 9/9 in 106.40 seconds,
  including the 32-cycle settings theme resource-lifetime regression.
  EXE SHA256: 404E67722A681B0C14A726D789878324E31A0C4DA3319D77B43935107BFAC552.
  No video/audio processing changes, capture session or latency measurement.
  This is a preview, not a public release; the prior F1 issue remains separate.

### Settings-wide disabled text audit (2026-10-03)

- The earlier correction covered SS_LEFTNOWORDWRAP labels but missed wrapping
  STATIC descriptions. Console LPCM 5.1 help therefore retained native embossed
  text in both ASIO and WASAPI Exclusive. The native dark/Korean/150% baseline
  differs from flat text by 11,611 pixels for that paragraph.
- The paint-only fix now covers SS_LEFT/SS_CENTER/SS_RIGHT/SS_LEFTNOWORDWRAP
  text, preserving line breaks, wrapping, alignment and mnemonic policy.
  Non-text statics are excluded. Enabled and high-contrast text still use native
  painting. Text retrieval no longer has the old fixed 2,048-character limit.
  No audio driver, capture, device enumeration or playback code was changed.
- Every actual settings STATIC is included in regression, even on hidden pages:
  48 controls x two themes x two languages x four DPIs = 768 cases; two successive
  paints each (1,536 comparisons) exactly match flat text references. The
  four-line surround description fits at every language/DPI. ASIO/Exclusive/
  Shared transitions retain correct enabled state and visible explanations.
- Native ASIO/Exclusive UI snapshots use a driver-free fixture. Dark Korean
  150% ASIO and light English 125% Exclusive images were visually inspected.
  Logs and snapshots: outputs/settings-disabled-text-audit.
  Focused regression passed 9/9 in 109.79 seconds, including 32 repeated theme
  lifecycles with no GDI/USER resource growth and the prior dropdown regressions.
  EXE SHA256: 235E11C0604014240A8E4C95645D581B69539433605F4EF1CE8AB18D0670C2E9.
  This remains a preview; the previously tracked F1 issue is outside this fix.

### Guide and diagnostics alignment (2026-10-03)

- Settings uses the existing shared shortcut list with an optional tab-separated
  format. SS_LEFTNOWORDWRAP expands each key to the same native tab stop, keeping
  description starts aligned for F1/F11/F12/Tab/Esc. Default plain shortcut text
  and all bindings/video-only markers are unchanged; no duplicate key table.
- Both guide columns start at y=140 DIP. The diagnostic paragraph reserves 32
  DIP instead of 96; its checkboxes and folder action use 16/8/16-DIP gaps.
  Checkbox targets remain 32 DIP high and the dialog size is unchanged.
- Geometry/text-fit tests now include all four diagnostic controls, multiline
  no-wrap text and expanded tabs. Native font metrics verify identical tab stops
  in both languages at 100/125/150/200%. Guide snapshots now cover every DPI;
  light Korean 150% and dark English 125% were visually inspected.
- Build, test logs and preview: outputs/settings-guide-alignment. No capture or
  audio/video path changes; the previously tracked F1 resource issue is separate.
  Focused regression passed 9/9. Packaged EXE SHA256:
  7D557AC84F51874B4540C8187C6A4740E2CBBB6CA5BD16FC249F1852622589AF.

### Audio mode transition repaint (2026-10-04)

- Reproduced with the production settings controller: a running Exclusive
  scan button was first enabled by the shared visibility helper and then
  disabled by its scan-state updater. The mode-change handler also painted
  intermediate status text and controls without the existing redraw scope.
- Visibility now receives the final scan/buffer availability. Scan-button and
  audio-status captions are only set when changed. Mode switches, scan-start
  UI changes and relevant asynchronous completions use a short redraw scope;
  thread joins remain outside the newly added scopes. Initial scan state is
  established before the rest of the mode UI is refreshed.
- Exclusive results arriving in Shared/ASIO are consumed/cached, but no longer
  rebuild the active device/buffer controls or overwrite the buffer choice.
  Shared completions do not refresh Exclusive/ASIO. Running/completion ownership,
  endpoint verification, completed-scan reuse and retry behavior are retained.
  The ASIO driver-owned buffer stays disabled after the shared visibility pass.
- A real-controller regression fails on the pre-fix enable/disable bounce.
  The fixed path passes 400 Shared/Exclusive transitions across dark/light and
  100/150% DPI, including no visible direct STATIC draws mid-transition, at most
  one final paint per affected control, unchanged captions, retained 5.1 state,
  late results, ASIO availability and a real empty-endpoint worker lifecycle.
  No capture/audio driver is opened by this fixture.
- Focused regression passed 9/9 in 114.20 seconds. Build/test logs and the preview
  package are under outputs/settings-audio-transition. Video/audio processing
  and latency paths are unchanged; no hardware latency measurement is claimed.
  The separately tracked F1 resource-baseline issue remains outside this fix.
  Packaged EXE SHA256:
  F2CDA88ED17044FE7C88EB4C5B6E98210A37A83BC4964101DC4C410325CA2DE1.

### Shared application UI (2026-10-04)

- Added AppPalette as the single color source for settings, F1 help and the
  audio-only view. Settings colors remain unchanged: neutral dark surfaces,
  neutral light surfaces with blue actions. AppSettings.settingsLightTheme
  remains the persisted key; the selector now says App theme. F1 follows the
  current settings selection or saved viewer preference. Audio-only also
  themes its exposed background and native caption; video and in-video OSD
  colors/HDR composition are not changed.
- F1 uses the same private Pretendard TrueType faces and 10/9/20-point type
  roles as settings, 20-DIP card padding, a footer divider and a rounded accent
  action. Audio-only keeps its existing full-card hit areas, aspect ratio,
  resize persistence, drag/wheel/double-click semantics and double buffer;
  only typography, neutral cards, edges and theme colors change. Warning and
  clipping colors have readable dark/light variants; high contrast uses
  system colors. No new capture/audio queue, driver action or network request.
- Native rendered previews cover both themes/languages, default and narrow
  F1 layouts, and minimum/large audio-only panels. Text-fit checks use the
  actual bundled fonts at seven DPI scales from 100% to 300%. Audio-only
  passes 1,000 theme/size/state paints with restored DC state and no GDI growth.
- The older F1 process-resource baseline warning was investigated separately
  from UI changes. Native diagnostics show retained IME/MSCTFIME windows after
  all F1 windows close. The strict resource test now isolates only its own
  test thread's IME using ImmDisableIME, warms native rendering, then checks
  a flat GDI/USER plateau. An additional normal-IME test preserves real input
  routing. Both run 320 window lifecycles and explicitly verify all four fonts,
  all three brushes, help HWNDs, F1/Esc/F12, wheel/Tab routing and owner shutdown.
  The application and the user's IME configuration are not modified.
  API scope: https://learn.microsoft.com/en-us/windows/win32/api/imm/nf-imm-immdisableime
- Logs, native snapshots and preview package: outputs/app-ui-unified.
  The 12-test focused suite passed after updating MonitorMoveTests' legacy
  palette expectations to check both saved app themes. Its geometry assertions
  were unchanged; final rerun of both F1 suites and MonitorMoveTests passed 3/3.
  Packaged EXE SHA256:
  0BEF49695B022F1246E71B5BD2F940F7B22A3D93B2D90C76C2DAAD86434973DE.

### Settings shortcut card (2026-10-04)

- Replaced the dense tab-delimited Help & diagnostics list with read-only
  native STATIC key labels and descriptions in a shared-theme rounded card.
  Uses the same shortcut table and video-only footer as F1, with 36-DIP row
  rhythm, 20-DIP card padding and vertically aligned key/action text. Diagnostic
  heading aligns with the card heading; its existing internal spacing stays
  16/8/16 DIP. Key decorations have no click handler, tab stop or hover state.
- Added assertions for all shortcut labels, actions, text fit, key/action
  alignment at fractional DPI, non-interactive semantics and visibility during
  50 guide/video/audio transitions. Native rendered snapshots cover both
  languages/themes and 100/125/150/200% scaling; inspected light Korean 150%
  and dark English 125%. Rendering is offscreen, without starting capture.
- Final focused regression suite: 12/12 passed (136.04 seconds), covering F1
  with/without IME, settings layout/theme/controller/store, mode cache, VSR,
  HDR/SDR pipeline, monitor moves and audio-only painting.
- Evidence and preview: outputs/settings-shortcut-cards (build logs, tests.log,
  screenshots and ZIP). Build and package executable SHA256 match:
  ECB94BEC67B7648CA0DCE4E53A796F4DEFDFB5FDC608EB546D464284C1A2F541.
  No capture, audio-processing or presentation-path change in this UI update.

### In-video OSD styling (2026-10-04)

- F3 audio OSD, transient volume/screenshot/VSR messages and Tab diagnostics
  now borrow the app's neutral dark palette, rounded panels, subtle edges and
  embedded Pretendard Medium/SemiBold typography. On-video panels deliberately
  remain dark regardless of the app's light theme. F3 aligns L/R with percentages,
  separates secondary level/status text, and indicates the enabled 200% limit.
  Existing gain-bar semantics, panel dimensions and hit regions are unchanged.
- OverlayStyle owns a DirectWrite in-memory font collection per renderer.
  The same existing TTF resources are loaded at renderer initialization, not
  per frame. Layouts/formats are released before unregistering the loader on
  reset. Unavailable modern DirectWrite/resource support falls back to system
  fonts. Private GDI font registration alone is not used as a DirectWrite source.
  Native tests verify actual embedded family/weight with no synthetic bold.
  API references:
  https://learn.microsoft.com/en-us/windows/win32/api/dwrite_3/nf-dwrite_3-idwritefactory5-createinmemoryfontfileloader
  https://learn.microsoft.com/en-us/windows/win32/api/dwrite_3/nf-dwrite_3-idwriteinmemoryfontfileloader-createinmemoryfontfilereference
- HDR outer panels retain neutral black at 90% opacity and the existing HDR
  compositor/UI-white mapping. SDR alpha is unchanged. No new frame queue,
  readback, wait or Present call in production; cached overlays still return
  immediately when their generation has not changed. Additional card drawing
  occurs only on overlay refresh. End-to-end display latency was not measured.
- Added OverlayUiTests using the actual GPU renderer, without opening any
  capture/audio device or visible test window: both languages, embedded font
  weights, diagnostic/text fit, 100/200% gain, clipping/hover states, all transient
  messages, 8,000 unchanged-cache calls and resource reset. Adjusted non-volume
  message text to 20 pixels so the longest English notifications retain two
  readable lines. Offscreen snapshots are in outputs/osd-ui-unified/screenshots.
- Focused regression suite passed 14/14 (136.89 seconds), including HDR/scRGB
  transparency/composition, SDR fidelity, VSR, settings, F1 and audio controls.
  Preview package: outputs/osd-ui-unified/LowLatencyCaptureViewer_OSD_UI_Preview.zip.
  Build/package executable SHA256:
  1F875DA3DAA9C09B95A7640C560B9B99C67435EA7F8A8E8AB176AFB347DF37FB.

### OSD boost indication (2026-10-04)

- F3 master percentage and transient volume text use the same dark-theme
  warning orange as Audio only when volume boost is enabled and master gain
  exceeds 100%. 100% remains the regular color. Channels, gain bars, red
  clipping status and unrelated screenshot/VSR messages remain unchanged.
  One master snapshot feeds the cached volume text, bar and F3 value.
- Added actual BGRA GPU readback assertions for both languages, boost on/off,
  0/99/100/101/150/200/100/99 transitions, clipping independence and notification
  color isolation. New brushes are initialized once and released on reset.
- Focused regression passed 7/7 (18.32 seconds). Logs, native previews and ZIP
  are in outputs/osd-boost-color. Preview executable SHA256:
  2D0640229CBBD9717078107EB36E2D68162417EF7C1F3AAF5C6D74326D6CC83E.

### VSR timing validation (2026-10-04)

- Added opt-in test-only `--timing-validation` to VsrExperimentTests, with
  `--720p` and `--reverse` variants. No production instrumentation or new Tab
  value yet. Capture/audio devices are not opened by this synthetic GPU test.
- RTX 3080 validation: timestamps spanning upload through an output-dependent
  GPU copy tracked the serialized OFF/ON completion delta within 0.05–0.21 ms
  in four runs. Bare VP and no-copy queries substantially undercounted.
  This is a GPU-path estimate, not actual display-added latency; real Present
  path validation and observer-overhead checks remain necessary.
- 8,640 measured timing frames; normal VSR/HDR/SDR/overlay regressions passed
  4/4. Production executable hash unchanged from the OSD boost preview.
  Full protocol, variability caveats and raw logs:
  [timing validation report](../outputs/vsr-timing-validation/REPORT.md).

### Tab VSR request status (2026-10-04)

- Tab now shows the renderer-applied VSR request state and input → actual video
  rectangle resolution. ON is explicitly activation-unverified; unavailable,
  rejected/OFF and failed/unknown states are distinct. Fullscreen 1:1 borders
  are excluded from the VSR display dimensions.
- No latency number, production GPU query, readback, additional Present or
  frame queue was added. The existing cached diagnostics gains one row within
  its unchanged panel dimensions. F6 invalidates that cache once when applied.
  Timing validation remains opt-in test-only tooling.
- Build succeeded. VSR/HDR/SDR/Overlay UI tests passed 4/4 (13.51 seconds),
  including both languages, all request states, one-line width/full-panel fit,
  F6 cache invalidation and actual pixel-perfect fullscreen video dimensions.
  Executable SHA256:
  C7D3A378AD096F4FAF2B4AF68ABE98D9DE1E8187FAD95D27CFFBB5972D2D15F2.

### Settings sidebar wordmark (2026-10-04)

- Replaced the generic CAPTURE heading with a compact icon + LLCV wordmark.
  The emblem reuses the capture corners and coral signal geometry of
  tools/make-icon.ps1, supersampled at the current DPI; no new external asset,
  font installation or image-generation dependency. The original app icon
  itself is unchanged.
- The wordmark follows light/dark foreground colors and remains a noninteractive
  STATIC with a native text name. High contrast falls back to native LLCV text;
  disabled text retains the flat-label renderer. Temporary GDI resources are
  released after painting. This is settings-window-only, not the video path.
- Offscreen previews at 96/120/144/192 DPI, both themes and languages are in
  outputs/settings-brand/snapshots. Pixel assertions cover the coral signal,
  visible wordmark, DPI geometry, non-tab-stop behavior and repeated paint
  resource counts. Build and regression logs are in outputs/settings-brand.
- Alignment refinement: the complete emblem/wordmark group is measured and
  centered on the sidebar axis, as is the native version label. Navigation
  captions move right by 3 DIPs while selection backgrounds/hit areas retain
  their position. Added a visible-ink centering assertion at all tested DPIs;
  updated geometry and caption-fit expectations. Refined previews and test
  logs are in outputs/settings-brand-centered.

### Content-fitted transient notifications (2026-10-04)

- VSR, screenshot and pixel-perfect notifications now fit their cached text
  height with 10-pixel top/bottom layout padding instead of reserving the
  82-pixel volume-meter panel. Volume notifications retain the meter geometry.
- GPU textures/quads remain unchanged; unused rows are cleared transparent.
  Bottom-anchored panels paint against the texture bottom, preserving the
  existing screen margin without stretching text or adding GPU allocations.
  No per-frame query/readback/wait was added.
- Native previews reviewed for one/two lines. Both languages and all four
  positions tested for fit and stale alpha pixels. VSR/HDR/SDR/Overlay tests
  passed 4/4 (14.68 seconds). Evidence: outputs/osd-fit.
  Separate executable: build-vsr-disabled/LowLatencyCaptureViewer_OSDFitPreview.exe
  SHA256: 032B3A013222A8E08A17749CB88BF9B4206FE12E988BD14A2390BCED2F31022E.

### Symmetric window-edge snap (2026-10-04)

- Entry and release share the same 20-DIP work-area edge distance, scaled by
  monitor DPI. The previous release threshold also said 20 DIPs, but measured
  cursor travel from latch time and could add a second attraction zone.
  The latch now retains the unsnapped edge offset and reconstructs that offset
  from cursor movement. Inside/exactly on the boundary snaps; beyond it releases.
- A changed work-area edge clears the old latch; width/height, independent axes,
  Shift bypass and fullscreen/disabled behavior are preserved. Video processing
  and presentation are unchanged.
- Simulated Win32 handler tests passed 384 edge/DPI/mode/origin/direction cases,
  corner release and monitor crossing, plus existing 2,400 topology/order replays.
  Focused MonitorMove/AudioOnlyView/VideoTransitionFaults/VideoTransitionStress
  tests passed 4/4. These are deterministic geometry tests, not a physical drag
  feel measurement. Evidence: outputs/snap-symmetric.
- The broader AudioCallbackTests failed earlier in TestSettingsCapabilityRefresh
  ("each selection applies exactly one fresh synchronous query"), before any
  snap/move path is exercised. This separate failure remains unresolved; do not
  report the entire suite as passing.
- Preview: build-vsr-disabled/LowLatencyCaptureViewer_WindowSnapPreview.exe.
  SHA256: C1A59CBA977B0FBFD23D94638F98B648045504D806DA24B2AFA67E7DB4C9E277.

### Reference links

- Chromium's `ToggleNvidiaVpSuperResolution` documents the interoperable driver
  extension ABI (GUID, version 1, method 2, enable word):
  https://chromium.googlesource.com/chromium/src/+/lkgr/ui/gl/swap_chain_presenter.cc
- Microsoft driver extension API:
  https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11videocontext-videoprocessorsetstreamextension
- mpv's manual explicitly distinguishes requesting an extension from its actual
  activation, which depends on hardware/driver settings:
  https://mpv.io/manual/master/#video-filters-d3d11vpp

The implementation uses the protocol values and API contract, not a copied
third-party renderer or bundled binary.

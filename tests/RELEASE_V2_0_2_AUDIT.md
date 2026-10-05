# v2.0.2 release-readiness audit

NOTE: The initial hashes/test result below precede the Exclusive follow-up.
See the latest follow-up section for current package hashes and validation.
Initial and intermediate packages are backed up locally.

Date: 2026-10-05. Release branch: agent/release-v2.0.2.
Base HEAD: ba3223f32599079fa2dc1ce3b9d3aa26dea1a823.
This records local preparation only: no commit, push, tag, publication or merge
was performed in this preparation pass.

## Scope

- Includes pending LLCV display branding, simplified viewer titles/device
  guidance, feature-label updates, optional installer shortcuts and task-list
  checkbox spacing. Driver-provided device names remain unchanged.
- Removes unused counters and obsolete translations; text lookup uses static
  string views, and private VSR histogram storage is excluded from production.
- Runtime, CMake, PE resources, installer, packaging defaults, README and
  current documentation align with 2.0.2. Historical release records remain.
- Installation identity, executable filename, saved-settings paths, capture
  selection and video/audio buffering algorithms remain unchanged.

## Build and validation

- Fresh MSVC x64 Release build in build-v2020-release completed successfully.
- LLCV_HDR_FRAME_AUDIT, LLCV_HDR_SCRGB_PROTOTYPE and LLCV_VSR_EXPERIMENT are OFF.
- Full CTest passed: 45/45, zero failures or skips, 377.04 seconds; exit code 0.
  Coverage includes settings/theme/language/persistence, F1, controller/F6,
  SDR/HDR/HDR+VSR GPU paths, screenshots, overlays/window transitions,
  audio callbacks, PCM/5.1, extended audio replay and video stress tests.
- Existing ASIO SDK deprecation/narrowing and test monitor-name shadowing
  warnings remain; this is not a warning-free build.
- git diff --check passed.
- Earlier same-change checks: private VSR test build passed 1/1; 30,000
  post-initialization text lookups produced zero temporary heap allocations
  versus 20,000 in the baseline. These are separate cleanup measurements,
  not an end-to-end latency benchmark (outputs/code-cleanup).
- Earlier installer layout-only preview included the production
  WizardLayout.iss: reproduced the old clipped checkbox and verified both
  corrected checkbox outlines/marks with mouse and keyboard selection.
  This was not an installation or upgrade (outputs/llcv-branding).
- Current logs: outputs/release-v2.0.2/{configure,build,tests,package,installer,
  package-verification,defender}.log.

## Packaging

- Portable ZIP: all 56 entries verified against staging file hashes.
- Installer manifest: all 56 payload hashes verified against current sources.
- Packaged EXE matches the fresh production build. App version is 2.0.2;
  installer numeric version is 2.0.2.0. Product name is LLCV.
- Both packages include GPL, ASIO SDK/host and Pretendard notices, Korean and
  English README files, and the 2.0.2 release notes.
- ZIP contains no user settings, runtime logs, test executables, PDB/OBJ/ILK
  files or build folders. Published 2.0.1 assets were not overwritten.
- Non-remediating local Defender scan of outputs/v2.0.2 found no threats.
  This does not establish other antivirus results or false-positive clearance.
- App and installer remain unsigned (Authenticode: NotSigned); no signing
  or SmartScreen reputation is claimed.
- Assets and SHA256SUMS.txt are in outputs/v2.0.2.

SHA-256:

- App: 022296CE082CDC3A105DB6A1FCBC12E9E209AC8421575255138BACE55F6C0A49
- Installer: BB9B50B52F7913DDE4A1F6816053B8DD377FFF5FD1396455DEDAACA34E9848A8
- ZIP: 985BD1E834ADC2D7BCDEA8FA3A19974A4BF1438353B857B11658196A0188F3E0

## Limits and remaining checks

- No new physical console capture, HDMI-to-panel latency measurement,
  physical 5.1 speaker-channel check, hardware endurance playback or
  clean-machine installation/upgrade was performed in this pass.
- Label changes do not expand device compatibility. ASIO still needs a
  suitable driver; console LPCM 5.1 still requires compatible PCM capture
  and WASAPI Shared. VSR acceptance does not prove driver enhancement.
- Existing fixed settings-window dimensions can exceed small/high-DPI
  monitor work areas; in-window layout tests do not establish monitor fit.
- Automated regression results do not guarantee every device combination
  or a measurable reduction in physical latency.

## Exclusive preflight follow-up (2026-10-05)

- Unified buffer-alignment initialization between renderer and preflight,
  including fresh-client retry after AUDCLNT_E_BUFFER_SIZE_NOT_ALIGNED.
- Exclusive preflight submits whole silent packets without Shared-style
  padding subtraction. Timing uses the actual aligned period and a high-resolution
  monotonic clock instead of GetTickCount64's coarse interval steps.
- ProbeVersion=2 invalidates old cache and legacy single-device verification
  together. Completed supported/unsupported/retry-required attempts persist;
  mode changes/reopening do not automatically retry them. Explicit rescan still
  works, and canceled scans do not create a completed verdict.
- Busy/unavailable/policy-blocked devices and unstable measurements remain
  unverified, not permanently unsupported. Transient initialization failures
  stop the candidate loop instead of immediately retrying every buffer size.
- Targeted regressions passed 5/5 (9.33 seconds) before the final clock refinement:
  aligned initialization/lifetime, renderer packets, whole-packet preflight,
  actual-period acceptance, starvation rejection, cache migration/persistence,
  and controller mode-switch/no-rescan behavior.
- Hardware: the intermediate build passed ORA by Kanto at 15 ms; logs exposed
  coarse 16 ms interval readings on healthy shorter periods. After clock
  refinement the then-current default TOPPING USB DAC passed at 5 ms: 999 events,
  average/max 5.00/6.00 ms, 239760 submitted frames, CLI exit 0.
  These are silent Exclusive playback-event checks, not captured-audio listening
  or HDMI-to-panel latency measurements. Final hardware log is under
  outputs/release-v2.0.2/exclusive-device-profile-final/LowLatencyCaptureViewer/logs.
- Tests used isolated profile directories; the user's saved settings and
  running viewer were not overwritten or terminated.
- Rebuilt installer and portable ZIP: 56 ZIP entries and 56 installer payload
  hashes verified. Local non-remediating Defender scan found no threats.
- Current app SHA-256: 9B9A7540EC005BAA3FE06B5AFA900C0F65DEDF745E91A9B849884943C8036945
- Current installer SHA-256: 8FB6613A67CE0158DCC70AEBA2AF5CBFAD6AE5F6A486199E9CCEB6550714F381
- Current ZIP SHA-256: 903D8DA7A17479BB95E72A8F40CCFC011C46005F77A2483CC467DEDA503865D4
- Logs: outputs/release-v2.0.2/exclusive-*.log. Previous unpublished packages
  are recoverable in outputs/release-v2.0.2/before-exclusive-fix.
- Final full-suite validation after the clock refinement: 45/45 passed,
  zero failures or skips, 392.75 seconds, exit 0 (exclusive-full-tests.log).
- Final git diff --check passed. No publication, merge or install performed.

## Audio hot-path optimization follow-up (2026-10-05)

- PCM Push/Pop now copy at most two contiguous spans, retaining the same mutex,
  queue capacity, frame ordering, overflow policy, occupancy publication and
  overrun accounting. No change to conversion, gain or resampling algorithms.
- Exclusive diagnostic clock acquisition/reads and per-second statistics are
  skipped when neither file logging nor the diagnostic console is enabled.
  Render packets, source filling, Tab telemetry, timeouts and failure/recovery
  control paths are unchanged; detailed diagnostics remain available when enabled.
- Video upload, rendering, VSR, HDR and presentation paths are unchanged.
- Added 32,000 deterministic FIFO/reference operations across stereo/5.1 and
  capacities 1/3/31/480, including wrap, overflow, oversized writes, clears,
  zero-length operations and output guards. Existing resampler golden hashes
  still check exact PCM output. Targeted audio tests passed 5/5 (1.83 seconds).
- WASAPI fake-COM coverage now runs 24 Shared/Exclusive/diagnostics scenarios:
  identical packet behavior and resource cleanup with diagnostics on/off;
  clock reads are retained when on and absent when off.
- Local Release microbenchmark, median of 5 runs, each with 20,000 Push/Pop pairs:
  480-frame stereo 69.197 -> 1.109 ms; 480-frame 5.1 72.561 -> 2.312 ms.
  All six channel/block-size benchmark checksums match. This measures the PCM
  ring alone, not total CPU use, audible latency or physical capture latency.
- Benchmark source, baseline source copy, logs and executables are under
  outputs/av-optimization and are not application/package dependencies.
- Refreshed unpublished 2.0.2 installer/ZIP: all 56 ZIP entries and 56 installer
  payload hashes verified. Local non-remediating Defender scan found no threats.
  Previous packages remain recoverable in outputs/av-optimization/before-packages.
- Current app SHA-256: 65B94FF5FFC1D5A673758C4F54964BCFBF10BABE76463A6A1F283E5DA8321759
- Current installer SHA-256: 19349893D627E17F7D4B5D1E73E0232588F0B4053D6015B9EB17FDA76277980F
- Current ZIP SHA-256: 682F05749378A266DD894008DB97D717DEF4A53149E9D5B50BD0A653A113C82F
- Final full suite: 45/45 passed, zero failures/skips, 374.27 seconds, exit 0
  (outputs/av-optimization/full-tests.log). Final git diff --check passed.
- No new hardware listening/capture or physical latency measurements in this
  optimization pass. No publication, merge, install or running-app replacement.

## Additional review fixes (2026-10-05)

- Preserved valid user-selected Exclusive buffers across endpoint results,
  scan completion and endpoint changes. Only invalid or below-minimum values
  are raised to the verified minimum; a 30 ms choice is no longer reset by
  another device completing at 5/10 ms.
- Completed endpoint results now persist when consumed, without waiting for
  all devices. Normal completion and dialog shutdown share worker join,
  pending-result drain and cancellation cleanup. Untested endpoints remain
  unknown; supported/unsupported/retry-required results retain their meaning.
- Consolidated scan UI refresh and minimum-buffer application; duplicate
  results cannot inflate completion counts. Kept legacy settings migration.
- Added missing English endpoint/status/action translations and placeholder
  regression checks, preserving Korean captions.
- Unified Exclusive timing acceptance and diagnostic reason selection through
  EvaluateExclusiveTiming. Existing 40 ms buffer, 5 s observation, 98% supply,
  90% event-count and maximum-gap limits are unchanged.
- Removed the redundant Exclusive initialization wrapper and duplicate error
  logging. The shared fresh-client alignment retry remains unchanged.
- Targeted tests: 6/6 passed, 28.25 seconds. Coverage includes controller
  multi-endpoint completion, user buffer preservation, partial cancellation,
  unread results after HWND destruction, duplicate delivery, idempotent
  finalization, timing boundaries, translations, settings and WASAPI packets.
- Test fixtures suppress user settings writes and do not enumerate/open
  physical audio devices. Production capture/render/PCM hot paths are unchanged
  except for removing the setup-only wrapper; no new per-frame work was added.
- Full regression: 45/45 passed, zero failures/skips, 382.41 seconds,
  including HDR/VSR/SDR, stereo/5.1 replay, extended replay, screenshots,
  settings/theme, recovery and video stress tests. Final git diff --check passed.
- Logs: outputs/additional-review/build-fixes.log, targeted-fixes.log and
  full-fixes.log. The build retains two existing test-fixture shadowing warnings
  concerning the monitors identifier; no new compiler warnings remain.
- Updated local build: build-v2020-release/LowLatencyCaptureViewer.exe,
  SHA-256 1A32F720E6100E8210F3A6283E0CFBAA5AEE200F957928D5268174C1F35577DB.
  Previous installer/ZIP artifacts above have NOT been refreshed in this pass
  and do not yet contain these follow-up fixes.
- No publication, merge, install, running-app replacement or new physical
  latency/listening measurements in this pass.

## Final distribution preparation (2026-10-05)

- User approved v2.0.2 publication and merge after the follow-up fixes and
  Korean/English README/release-note review. README titles are
  "LLCV - Low Latency Capture Viewer".
- Final build reports no work to do; the EXE is byte-identical to the
  follow-up build that passed all 45 tests (382.41 seconds).
- Rebuilt both distribution assets with the latest README/release notes.
  Verified all 56 ZIP entries and 56 installer payload hashes, notices,
  version fields and executable identity. No user settings or test binaries
  are included. Private HDR/VSR experiment build flags remain OFF.
- Non-remediating local Defender scan of final outputs/v2.0.2: no threats,
  exit 0. App and installer remain unsigned; this is not false-positive
  clearance from other engines or a SmartScreen reputation claim.
- App SHA-256: 1A32F720E6100E8210F3A6283E0CFBAA5AEE200F957928D5268174C1F35577DB
- Installer SHA-256: 2C19E48F0F38C1026857030ADDC24CE4653F5208274C4FBB3E706035B95E9ABB
- ZIP SHA-256: A1C23DE759CA0CEEA5741294CEC1542FBB31E574ED78368D82B33C243AF14907
- Logs: outputs/additional-review/release-{build,installer,package-verification,defender}.log.
  Earlier generated packages remain recoverable under
  outputs/release-v2.0.2/before-final-publication.
- No new hardware endurance, physical latency, installation or upgrade run.
  This section records verification immediately before the requested publication.

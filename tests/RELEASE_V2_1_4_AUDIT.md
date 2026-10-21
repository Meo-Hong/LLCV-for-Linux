# LLCV 2.1.4 release preparation

## Status

Prepared on 2026-10-21 from v2.1.3 (`0d953ce`) plus the current uncommitted fixes.
Version metadata and Korean/English release documentation target 2.1.4.
This is not a published release or merged branch. User settings and the installed app are unchanged.

The held audio-input recovery prototype is isolated behind the default-OFF
`LLCV_AUDIO_INPUT_RECOVERY_DIAGNOSTIC` build option. Normal builds do not compile its
watchdog, retries, retry state or error strings. The existing WASAPI/ASIO output recovery
and capture callback diagnostics are unchanged. The prototype is not a confirmed HD60S+
silence fix and is not advertised in the public release notes.

## Intended changes

- F2 opens the normal default settings page; the Recording tab remains available.
- Larger PCM packets use bounded recording buffers rather than failing at 100 ms.
- Recording finalization rechecks queued input after producers have left.
- Malformed numeric settings fall back safely; channel volume is clamped before rounding.
- An empty diagnostic message no longer disables subsequent file logging.
- Audio filter normalization is summed once per frame, retaining identical PCM output.

The nested F2 cancellation fix is part of completing the new settings behavior,
not a separately advertised regression in an already published version.

## Verified before the version update

| Check | Result | Evidence in local outputs |
| --- | --- | --- |
| New cancellation test against the pre-fix code | Failed at the intended exclusive/live-settings assertion | `f2-cancel-before-20261021.log` |
| Same test after the fix | Passed, 1/1 | `f2-cancel-after-20261021.log` |
| Related regression suite | Passed, 51/51, 321.14 seconds | `f2-cancel-regression-20261021.log` and `.xml` |
| All configured Release targets | Built successfully | `f2-cancel-fixed-build-20261021.log` |
| Recording-disabled main source | Syntax-only compilation passed; not a full package build | `f2-cancel-no-recording-build-20261021.log` |

The 51-test run covers shortcuts/settings, active and paused recording cancellation,
storage-delay splitting, mute/gain, MJPEG/NV12/YUY2 input and crash recovery,
HEVC/AV1 recovery, audio input changes/boundaries and playback lifecycle state.
It is a selected regression suite, not a claim that every registered test ran.

Cancellation tests use hidden HWNDs, synthetic recording input, injected modal answers
and simulated render acknowledgements. They do not verify a real monitor's exclusive
fullscreen handover, calibrated HDMI latency, all capture devices, or physical AV sync.

## Version and documentation recheck

- All configured Release targets rebuilt successfully after the 2.1.4 metadata update.
  The local executable reports FileVersion and ProductVersion `2.1.4`.
  Evidence: `outputs/release-v214-preparation-build-20261021.log`.
- Six checks passed in 28.28 seconds: `SettingsControllerTests`, `ViewerLifecycleTests`,
  `ExclusiveViewerLifecycleTests`, `RecordingSettingsCancelTests`, `PackageDocsTests`
  and `ReleasePackageTests`. Evidence: `outputs/release-v214-preparation-tests-20261021.log`
  and `.xml`.
- `ReleasePackageTests` validates temporary CMake-install staging, including notices,
  recording DLLs, documents and executable version. It does not create or validate a
  distributable ZIP or Inno installer. Prototype isolation was verified separately below.
- The configured suite uses PowerShell 7 for Unicode document paths. A separate manual
  Windows PowerShell 5.1 invocation failed on a Korean fixture filename before the
  configured test passed; this is not counted as a product regression or a test pass.

## Diagnostic isolation and cleanup

- The normal audio-only capture function matches v2.1.3 source after selecting the
  default preprocessor branch, ignoring whitespace.
- `AudioRecoveryBuildBoundaryTests` preprocesses the real main source in both modes:
  normal output has no recovery symbols; diagnostic output retains all four checked symbols.
- Normal recording-enabled, diagnostic recording-enabled and recording-disabled builds
  all compile and link. The diagnostic EXE has its own filename and FileVersion suffix.
  Evidence: `outputs/v214-isolation-build-20261021.log` and `outputs/v214-variant-builds-20261021.log`.
- Actual diagnostic CMake install, portable ZIP creation and Inno compilation are rejected.
  Renaming the diagnostic EXE also fails the ordinary version check. The build-directory
  marker covers stale regular EXEs; this case is included in `ReleasePackageTests`.
  Evidence: `outputs/v214-diagnostic-gates-20261021.log`.
- Recovery algorithm tests remain available without enabling the feature in the product.
  Their abort-only callbacks were replaced with explicit unexpected-call checks, removing
  the resulting unreachable-code warnings without weakening the assertions.
- No additional unused product function was confirmed. Existing output recovery, callback
  timestamps, asynchronous diagnostics and `recording-prototype.ini` compatibility stay intact.

## Final regression run after isolation

- Passed **145/145 tests**, zero failures and zero skipped cases, in **755.89 seconds**.
  Evidence: `outputs/v214-isolation-regression-20261021.log` and `.xml`.
- Coverage includes settings/shortcuts, nested settings cancellation and simulated exclusive
  lifecycle transitions; audio callback, PCM, mute and device-switch handling; recording
  packet boundaries, finalization, pause/split, storage delay, color conversion, independent
  decoding and crash recovery; and documentation/package/build-boundary checks.
- The new preprocessing boundary check and stale-regular-EXE packaging rejection both pass
  as part of this run. Diagnostic algorithm tests still pass without enabling the prototype
  in the regular application.
- Of 155 registered tests, the ten labelled `GPU` were deliberately excluded, not counted
  as passes: `VsrExperimentTests`, `VideoClearRegression`, `MjpegRecordingPlaybackTests`,
  `HdrPipelineTests`, `HdrVsrPipelineTests`, `SdrPipelineTests`, `OverlayUiTests`,
  `RecordingHevcSyncTests`, `RecordingHevcArrivalSyncTests` and `RendererResizeTests`.

## Local release artifacts

- Created the regular portable ZIP and Inno installer under `outputs/v2.1.4/`; neither
  was installed, uploaded or published.
- Extracted the ZIP and checked its version, required DLLs, notices and documentation links.
  All 83 payload files match the installer manifest by name and SHA-256, including renamed
  notices. The packaged application hash matches the tested normal build.
- The installer reports version `2.1.4.0`; the application reports `2.1.4`.
  Evidence: `outputs/v214-release-assets-check-20261021.log`,
  `outputs/v214-portable-build-20261021.log` and `outputs/v214-installer-build-20261021.log`.

| Artifact | SHA-256 |
| --- | --- |
| `LowLatencyCaptureViewer_v2.1.4_Setup.exe` | `820248EAB3810EFF7A81D81FE013411118D9901E0B12B4960BA1134715AE32E9` |
| `LowLatencyCaptureViewer_v2.1.4_x64.zip` | `596D96A541E4B0B5149782A404EF80DD13877AB81D97CC8CB5498A9FAD4C4310` |

## Performance evidence and limits

- Normal audio-only capture keeps the released function body and excludes the diagnostic
  watchdog/retry code at preprocessing time, not merely through optimizer elimination.
- PCM golden checks compare sample counts and hashes against the pre-optimization output
  across block sizes, starvation and 0.999/1.000/1.001 resampling ratios. Surround checks
  compare six channels against three independent stereo pipelines bit for bit.
- Shared playback replay tests run the real PCM/DSP with a virtual clock. They cover
  queue overflow, underrun, reserve and stall handling; elapsed test times are not CPU
  performance measurements.
- This pass does not measure real HDMI/display latency, live CPU usage or calibrated
  audiovisual sync, and does not test physical exclusive-fullscreen monitor handover.

## Release checklist

- [x] Isolate the held audio-input recovery prototype from the release build without discarding it.
- [x] Rebuild and run regressions after that release-scope decision (145/145; GPU-labelled exclusions listed above).
- [x] Verify the actual portable ZIP and Inno installer payload, including versions, DLLs and notices.
- [x] Record physical-device checks separately: no live capture, monitor switching, physical latency or AV-sync remeasurement in this pass.
- [x] Review source/test and release-document changes, including new untracked headers/tests.
- [ ] At commit time, stage the reviewed source/test files explicitly; do not stage `outputs/` wholesale.
- [ ] Publish and merge only after explicit user authorization.

Existing UI screenshots keep their accurate 2.1.3 render captions because this preparation
does not regenerate them. Historical release notes and feature-introduction dates remain unchanged.

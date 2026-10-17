# v2.1.0 release preparation audit

Date: 2026-10-17 (KST)

Branch: `agent/recording-prototype`
Base: `c4a2ab4` (v2.0.5 merge)

## Scope

Prepare the existing SDR recording, exclusive-fullscreen and display-placement work for v2.1.0. Stop the direct-display/latency experiments. No new live capture, display-mode experiments, installation, commit, push, merge or release publication was performed during this preparation.

## Cleanup and corrections

- Promote recording to the normal build option `LLCV_RECORDING`; retain the executable name, installation path and existing settings paths.
- Remove the production automatic recording benchmark header, startup switches and frame hook. The independent test benchmark and historical output evidence remain outside the package.
- Restore normal settings persistence in recording-enabled builds. Smoke tests and private probes still avoid writing user settings.
- Fix F5 source-size restoration when the capture size matches the monitor but the saved display size is smaller. The saved display resolution is unchanged; a subsequent normal fullscreen transition uses that selection.
- Update fullscreen labels, six-page recording UI snapshots and shortcut expectations. Remove the monitor-test fixture's shadowed name.
- Package exactly the three minimal FFmpeg mux DLLs with their licenses, notices and build configuration. Reject incomplete runtime packages before release.
- Refresh Korean/English README, usage/video/build/release/troubleshooting documentation, add recording guides and user-facing release notes.

The old `recording-prototype.ini` filename is intentionally retained for settings compatibility; it is not an unused file. Historical experimental output/build directories were not deleted.

## Verification

| Run | Result |
| --- | --- |
| Fresh Release build, recording enabled | Passed |
| Initial full CTest run | 118 cases; 117 passed, one stale F11-caption expectation failed; none skipped |
| After correction and new coverage: focused CTest run | 19/19 passed; none skipped |
| Latest result per distinct test name across both runs | 120/120 passed |
| Final complete run after 1080p/60fps defaults and presentation wording | 120/120 passed; no skipped/disabled tests; 777.61 seconds |
| Fresh playback-only Release build (`LLCV_RECORDING=OFF`) | Passed; no FFmpeg imports |
| Native offscreen Korean/English settings and F1-help snapshot export | Passed; reviewed video, recording and shortcut layouts |
| Package validation | Passed, including missing-runtime negative regression |
| Portable ZIP integrity | All 79 files match their staging SHA-256 values |
| Inno Setup compiler manifest | All 79 source file hashes match current inputs |
| Diff whitespace check | Passed |

The earlier 120 latest results combine the initial run and focused rerun. The final
complete run independently passed all 120 tests on the final application sources.

New recording profiles now default to 1080p/60fps limits. Explicit saved choices,
including native size/rate, are preserved; legacy profiles with missing cap keys
retain their earlier native behavior. Tests verify defaults, 4K downscaling,
lower-resolution/rate input, and existing-profile migration.

Final-run evidence: `outputs/release-v210-final-build.log`,
`outputs/release-v210-final-tests.log`, `outputs/release-v210-final-tests.xml`.

Full regression includes recording pause/resume, split boundaries, storage failures, media sync checks, forced-exit container preservation, HDR/VSR/OSD and lifecycle tests. The focused run covers the final F5 geometry change, actual video-processor destination, fullscreen transitions, settings persistence and UI/package changes.

Evidence (local, not shipped):
- `outputs/release-v210-tests.log`, `outputs/release-v210-tests.xml`
- `outputs/release-v210-focused.log`, `outputs/release-v210-focused.xml`
- `outputs/release-v210-clean-build.log`, `outputs/release-v210-off-build.log`
- `outputs/release-v210-images.log`
- `outputs/release-v210-installer.log`
- `outputs/v2.1.0/LowLatencyCaptureViewer_v2.1.0_Setup-manifest.txt`

The playback-only clean build still reports existing C4996 warnings in upstream ASIO host code. That third-party implementation was not changed merely to suppress warnings.

## Prepared artifacts

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| LowLatencyCaptureViewer_v2.1.0_Setup.exe | 6,107,324 | 13625DFAA16777F2A0BD9AF676D4EB35CE6B53CD578CFAC2E1910C3703A100C6 |
| LowLatencyCaptureViewer_v2.1.0_x64.zip | 5,763,426 | 9A9FA00831CF39D050DC5938649281011EF2EFB9EA0CE4398494785A1E6C6BD2 |

Final artifacts are under `outputs/v2.1.0-release/`. Expanded portable contents total 12,117,301 bytes. No SDK, test executable, FFmpeg command-line tools, settings or diagnostic logs are shipped. The previous `outputs/v2.1.0/` folder remains in use by a running viewer and was not replaced; files removed by the interrupted staging cleanup were restored from its original ZIP without overwriting loaded files.

The final ZIP was extracted into a fresh validation directory and passed the
package/document checks. All 79 ZIP entries match staging, the staged executable
matches the tested Release build, and all 79 installer source-manifest hashes
match current inputs. The final playback-only build and documentation snapshot
export also passed. Evidence: `outputs/release-v210-final-off-build.log`,
`outputs/release-v210-final-images.log`, `outputs/release-v210-final-package.log`,
`outputs/release-v210-final-installer.log` and
`outputs/v2.1.0-release/LowLatencyCaptureViewer_v2.1.0_Setup-manifest.txt`.

Documentation-only follow-up: added the existing OBS/exclusive-fullscreen comparison from
`outputs/obs-comparison/results.json` and `outputs/obs-comparison/window-results.json`
to the bilingual release notes and video guides. The tables compare windowed,
borderless-fullscreen and exclusive-fullscreen results side by side against the
corresponding OBS projector baseline, with conditions directly below. Both
fullscreen rows use the same OBS fullscreen projector measurement.
The 33.3ms versus 11.4ms values are matched-frame ETW display-event estimates, not physical
input-to-photon measurements. Shared-GPU source interference and missing-frame limitations
are stated. No measurements or application builds were rerun; both packages were rebuilt,
their 79 file hashes rechecked, and packaged documentation links validated.

## Limits / follow-up

### Readiness review follow-up (2026-10-17)

- Encoder discovery now starts when the Recording tab is opened, rather than
  whenever settings opens. Closing sets a cancellation flag before worker
  cleanup, skips remaining probes, and destroys the dialog before joining.
  In-flight driver calls still finish normally; they are not forcibly interrupted.
- Recording storage information is queried on page entry or folder changes,
  not on every codec/quality edit or polling refresh.
- Valid custom duration/size thresholds from saved profiles remain visibly
  selected with exact units instead of showing a blank preset list.
- The recording tab now states NV12/YUY2 SDR and 16-bit PCM stereo support.
  Existing no-scroll layout is preserved.

Validation of this follow-up: 27/27 targeted regressions passed in 152.29 seconds;
5/5 HEVC/AV1 preservation and forced-exit recovery checks passed in 63.23 seconds.
The settings regressions cover lazy discovery, close cancellation order,
storage-query reuse, custom thresholds, and KO/EN text fitting across DPI and
compact layouts. Playback/HDR/VSR/OSD, pause/split, HEVC sync and lifecycle
checks passed. No capture/render scheduling or recording packet processing
was changed, and no new live latency measurement was performed.

The playback-only build and bilingual documentation snapshots were regenerated.
The screenshot fixture applies its deterministic storage caption after page entry,
so documentation does not expose the build machine's free-space reading. Final
installer and portable packages each contain 79 verified files; ZIP contents and
installer source-manifest hashes match staging, and package/link checks pass.

Evidence: `outputs/release-v210-polish-build-final.log`,
`outputs/release-v210-polish-tests.xml`, `outputs/release-v210-polish-tests.log`,
`outputs/release-v210-polish-recovery.xml`,
`outputs/release-v210-polish-recovery.log`.

### Remaining support limits

Presentation wording follow-up: the choices now read Low latency (standard) and
Low latency (exclusive fullscreen), without a minimum-latency claim. Bilingual
help, guides, README, release notes and screenshots explain their shared no-VSync
behavior and exclusive output's environment-dependent benefit. No presentation
logic, saved values, ordering or defaults changed. SettingsViewTests,
RecordingThemeTests and PackageDocsTests passed (3/3, 75.04 seconds); both release
and playback-only builds succeeded. New ZIP contents and installer source hashes
were verified against all 79 staged files.

- Recording supports NV12/YUY2 SDR and 16-bit PCM stereo input. MJPEG/HDR/5.1 and audio-only recording remain unsupported; playback supports more formats.
- Encoder availability is detected locally. This audit does not establish real Radeon/Intel hardware behavior or actual AV1 hardware encoding on an untested GPU.
- Fragmented MP4 protects completed written portions, not an unwritten final fragment or data lost from OS/device caches.
- No new optical end-to-end latency or live A/V sync measurements were made. Automated results are not a universal no-performance-impact or no-bugs guarantee.
- Installer compilation/manifest validation does not substitute for a clean-machine installation test. The prepared installer was not installed.
- The user authorized release after the final validation; the 1080p/60fps default request is included in the verified application sources.

# v2.0.1 release-readiness audit

Date: 2026-10-04. Release branch: agent/release-v2.0.1.
Base HEAD: 073574393750b643fb5b56cd79d08f91bb7c6839.
This record covers local validation before publication. No installation was
performed; publication and merge status are recorded separately on GitHub.

## Scope

- Includes pending native HDR10/YUY2 VSR, rounded viewer corners, selective
  overlay updates and rendering-adapter vendor gating.
- Runtime label, CMake, PE resources, installer, packaging defaults and current
  documentation headers aligned to 2.0.1.
- Korean/English README and release notes describe functionality and retain
  experimental VSR limitations. Historical release notes are unchanged.
- Added v2.0.0 -> v2.0.1 installer-discovery and equal/older-version rejection
  tests. The isolated UpdateCheckerTests run passed.

## Build and validation

- Fresh MSVC x64 Release build in build-v2010-release completed successfully.
- LLCV_HDR_FRAME_AUDIT, LLCV_HDR_SCRGB_PROTOTYPE and LLCV_VSR_EXPERIMENT are OFF.
- Full CTest: 45/45 passed, zero failures or skips, 422.15 seconds.
  Includes HDR+VSR/SDR GPU buffers, vendor/F6/controller gates, screenshot
  conversion, settings layout/theme/persistence, help/IME lifetime, overlays,
  rounded corners/window transitions, PCM/5.1 and extended audio-clock replay,
  update discovery and video fault/stress coverage.
- Follow-up: VSR caption is now "NVIDIA VSR (F6)" and the Korean hint reads
  "SDR / HDR10 · F6으로 전환". Regular windows keep their system border;
  only borderless windows suppress the outline. Both use DWMWCP_ROUND.
  Rebuilt the app and relevant fixtures; SettingsView, MonitorMove,
  WindowGeometry and WindowCorners passed 4/4 (21.82 s). The full 45-test run
  above predates this narrow follow-up. Final packages were rebuilt and their
  installer payload hashes and local Defender scan checked again.
- git diff --check passed.
- Final publication pass after the wording/border changes: rebuilt all targets
  and passed the full suite again, 45/45 with zero failures/skips (401.67 s).
  Production EXE SHA-256 remained identical to the packaged binary below.
  Logs: outputs/release-v2.0.1/publish-build.log and publish-tests.log.
- Compiler warnings: existing ASIO SDK deprecation/narrowing warnings and two
  monitor-name shadowing warnings in the test compilation; not a warning-free build.
- Logs: outputs/release-v2.0.1/{configure,build,tests,installer,defender}.log.

## Packaging

- Portable ZIP: 56 entries verified against the staging file hashes.
- Inno Setup: 55 payload hashes verified against current source files.
- Packaged app matches the fresh production build; app version is 2.0.1 and
  installer file version is 2.0.1.0.
- Both packages include GPL, ASIO SDK/host and Pretendard OFL notices, both
  READMEs and the 2.0.1 release notes.
- No user settings, runtime logs, test executables, PDB/OBJ/ILK or build folders
  found in the ZIP. The installer uses the same verified production EXE.
- Non-remediating local Defender scan of outputs/v2.0.1 found no threats.
  This does not guarantee other antivirus/signature results.
- App and installer remain unsigned (Authenticode: NotSigned); no signing,
  SmartScreen reputation or Microsoft false-positive clearance is claimed.

SHA-256:

- App: 0E9219064B6662DC781B6FFF51E788F24DEA1B9E76CEA7FC5BEF6F61AEF80691
- Installer: CBA22BF5F55EF3DAB935293D6089055BA0DB8C4A5091B1A2AB8D631AD4109159
- ZIP: 4C8608156B4CB810BCBBB80F969E708379CFB32F8E579CCC474887CCFD22CD84

## Known limits and remaining checks

- Existing fixed settings-window size can exceed small/high-DPI monitor work
  areas. The earlier UI audit measured 1268 x 860 outer pixels at 125% DPI,
  which cannot fit a 1366 x 768 display. This is not fixed in this preparation;
  control-within-window layout tests do not cover window-within-monitor fit.
- No new physical console capture, mixed-DPI drag, HDMI-to-panel latency,
  long-duration playback or clean-machine installer/upgrade run in this pass.
- Intel/AMD vendor gating is validated by injected policy/UI states, not by
  physical Intel/AMD hardware. NVIDIA testing uses this host's GPU.
- VSR request acceptance/ON is not proof of driver enhancement. No guarantee
  of elimination of intermittent stutter or reduction in physical latency.
- Previous native HDR-buffer, YUY2 pixel comparison and live NV12 evidence is
  documented separately in docs/vsr-hdr-validation.md and
  docs/vsr-all-formats-validation.md; it is not new testing of every device.

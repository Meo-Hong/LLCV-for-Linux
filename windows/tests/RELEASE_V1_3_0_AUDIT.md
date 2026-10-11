# v1.3.0 release verification

Verified on 2026-09-27. Release notes describe improvements, not a claim that
reported device-specific HDR symptoms or antivirus detections are resolved.

## Build and regression checks

- Fresh x64 Release build in build-v1300-release, MSVC 19.39 / Ninja / C++20.
- Static MSVC runtime. HDR frame audit and scRGB prototype both OFF.
- All 35 registered CTest tests passed, none skipped, 106.08 seconds.
- Coverage includes HDR/SDR GPU readback, overlay composition, monitor/output
  transitions, settings, latest-frame mailbox, raw sample layouts, MJPEG mock
  transforms and the native Windows decoder, WASAPI Shared/surround/extended/
  25 ms replay, Exclusive initialization/render mocks, and ASIO sample packing.
- Added explicit updater checks for v1.2.12 -> v1.3.0 and equal/older releases.
- Before the version bump, the same production changes also passed all 35
  tests. Dynamic video-format checks (960 cases plus 100 publication races)
  passed 100 repetitions. MJPEG and WASAPI focused checks also passed 100
  repetitions during development.
- Clean build warnings are from unchanged ASIO SDK sources/Unicode overrides
  and the existing MonitorMoveTests shadowed variable; no build errors.
- git diff --check passed.

## Pipeline review

- P010 remains HDR-only. Missing transfer/gamut information uses an explicit
  PQ/BT.2020 assumption, not automatic detection of actual HDR pixels.
- Supported AVerMedia/Elgato device tone-mapping requests happen at startup,
  after format selection and before graph connection/run. Identity/capability
  checks and bounded enumeration are retained; no periodic vendor writes.
- P010 + SDR-only compatibility output is rejected before capture-device access.
- Video/audio sample type changes and buffer bounds are checked. Equivalent
  announcements are accepted; incompatible changes stop for settings/restart.
- Output reset picks the newest available input without waiting for a new one.
- No additional frame queue, image copy/readback, steady-state logging, forced
  frame wait, or audio-buffer increase was introduced. Actual hardware latency
  is not established by mock callback microbenchmarks.
- Production HDR conversion remains P010 -> D3D11 Video Processor -> RGB10/PQ
  Flip output. The private scRGB comparison path is not enabled.

## Packages

- App file version 1.3.0; setup file version 1.3.0.0.
- EXE: 692,736 bytes; +24,064 bytes (23.5 KiB) versus public v1.2.12.
- Setup: 2,381,425 bytes. Portable ZIP: 441,419 bytes.
- All 47 files in the compiler-generated installer payload manifest and ZIP
  match their source/build SHA-256 hashes. The ZIP also has one empty docs
  directory entry.
- Application and both ASIO license notices are included, together with Elgato
  protocol attribution in DEPENDENCIES.txt. No settings, logs, test executables,
  PDBs, or build trees are shipped.
- PE imports contain only Windows system libraries; no external runtime DLL.
- Installer AppId and user-data preservation policy are unchanged.
- Installer was compiled and its manifest verified, not installed over the
  user's current installation during this pass.

| File | SHA-256 |
| --- | --- |
| LowLatencyCaptureViewer.exe | 872B0806BD4C03512303F5B1DE59085A9EC9AA962F76148BF1484245E826830F |
| LowLatencyCaptureViewer_v1.3.0_Setup.exe | E2A2EFF9986B1EC37AA5621D5B99CC9210D9DE27A7CEB363CBD283BC76799D24 |
| LowLatencyCaptureViewer_v1.3.0_x64.zip | 08FFFB4F3B5F64C133381E09D6C0A414DEAA4E95EEE4423D8D4FE383C8286795 |

## Defender and scope limits

- Local Microsoft Defender custom scans of the final EXE, Setup and ZIP each
  returned "found no threats", exit code 0, with definitions 1.459.417.0.
- Antivirus and real-time protection remained enabled; no exclusions were added.
  DisableRemediation applied only to these diagnostic custom scans.
- Executables remain unsigned. These checks do not establish VirusTotal status,
  download reputation, absence of future/other-machine detections, or a vendor
  false-positive review result. No external sample submission was made.
- No physical capture card or OBS was opened during release verification.
  Vendor commands use mocks; GPU tests use synthetic frames. Real-device HDR
  brightness/color, driver stability, long hardware playback and end-to-end
  HDMI latency remain outside this verification.
- Untagged HDMI content changes cannot be inferred reliably from frame storage
  alone. HLG, Full-range HDR, Blt HDR and independent HDR-to-SDR display mapping
  are not added by this release. HDR10 and console LPCM 5.1 remain experimental.

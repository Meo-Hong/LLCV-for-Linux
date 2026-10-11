# v1.3.1 local release-readiness audit

Date: 2026-09-29. Base commit: 65bd9fe, branch agent/release-v1.0.0 plus uncommitted changes.

## Scope and outcome

Reviewed screenshot capture/copy ownership, worker state transitions, cancellation,
PNG atomic publication, clipboard ownership, HDR conversion boundaries, help-window
lifetime and input isolation, settings persistence, UI colors, and packaging.
No new release-blocking functional defect was found in the exercised paths.
This is not a guarantee of zero bugs or physical-device compatibility.

- Clean x64 Release build in build-release-readiness; both private HDR options OFF.
- Final CTest: 37/37 passed, 102.21 seconds; SDR/HDR GPU tests executed, no skips.
- Earlier independent clean-build run: 37/37 passed, 117.28 seconds.
- Added inaccessible guard-page buffers at both ends for NV12/YUY2/P010 copying
  and conversion, including minimal dimensions, final chroma rows, and short input.
- Added 30 screenshot start/submit/cancel/stop cycles against a concurrent UI
  request/result consumer. No abandoned partial files or worker hangs observed.
- Added GDI/USER object-count checks across repeated bilingual F1 window lifetimes.
- Existing tests cover PNG round-trip, sRGB tagging, no overwrite, failed save,
  cancel-before/during encoding, timeout, owned sample copies, 4K cancellation,
  F1/Esc repeats, F12 routing, help scrolling and palette, settings round-trip,
  audio callbacks and audio-only painting.
- Updated execution guide with F1/F12 and screenshot instructions; aligned version
  metadata and local package defaults to 1.3.1. No unrelated processing changes.
- git diff --check passed. Only pre-existing monitor-variable shadow warnings in
  the monitor test translation unit remain in the final build log.

## Measured screenshot copy cost

Synthetic 3840x2160 source, three captures per format, no clipboard writes:

| Format | Source copy | Worker + save completion |
| --- | --- | --- |
| NV12 | 0.7073–0.9543 ms | 1797–1828 ms |
| P010 HDR to SDR | 1.4180–1.4707 ms | 2171–2172 ms |

The frame copy is synchronous on a requested capture; conversion and saving are
background work. These are local synthetic measurements, not HDMI-to-display
latency, frame-drop proof, or guarantees for other PCs. No video frame queue was added.

## Limits

- MSVC 14.39 AddressSanitizer binaries built, but all three attempted instrumented
  tests failed before test execution with interception_win.cpp:171. This check is
  incomplete, not a pass; instrumentation was not disabled to manufacture a pass.
  Guard-page and lifecycle checks do not replace complete sanitizer coverage.
- No new physical capture-device, long-duration audio playback, HDR display visual,
  or end-to-end latency test in this audit.
- User clipboard was not overwritten; real clipboard integration is not re-tested here.
- Installer compiled and payload manifest checked; no install/uninstall performed.
- No fresh antivirus false-positive clearance or signing claim.

## Local candidate packages

Directory: outputs/release-candidate-v1.3.1.

- Portable ZIP: 49 entries, required licenses/readmes/release notes present.
- No settings.ini, logs, build folders, PDB/ILK/OBJ, partials or test EXEs in ZIP.
- ZIP EXE and installer payload manifest match the verified build hash.
- EXE version 1.3.1; installer version 1.3.1.0; static MSVC runtime; imports are
  Windows components, with no sanitizer runtime or new external DLL requirement.
- Existing v1.3.0 release executable unchanged.

SHA256:

```text
EXE:   9824CF8FD7ABE4F1E4EDBCD1F2150D81E59593D25E1974D9813F9881A3689C46
ZIP:   083822407F989A7964FA428047FABFB7FEB44AC2D9E012FEF05840D822FC999D
Setup: 4FA2CB4B9962C2128B73EEBDD776EDF9203582599C6C1D5FE6FBDEBDBA7940BE
```

Logs are under outputs/release-readiness-*.log. Release notes are in
docs/release-notes-v1.3.1.md and describe additions/improvements and format scope.
No commit, push, tag, GitHub release, merge, or external submission performed.

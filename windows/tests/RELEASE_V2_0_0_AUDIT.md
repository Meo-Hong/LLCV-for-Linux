# v2.0.0 release-readiness audit

Date: 2026-10-04. Base: origin/main at 676ec5ecae67f569660a7851952158835262740a.
Release branch: agent/release-v2.0.0.

## Validation

- Clean MSVC x64 Release build in build-v2000-release.
- LLCV_VSR_EXPERIMENT, LLCV_HDR_FRAME_AUDIT and LLCV_HDR_SCRGB_PROTOTYPE OFF.
  Optional production VSR remains available; private per-frame timing is not enabled.
- Full CTest: 43/43 passed, 0 failed, 469.74 seconds. No skipped tests.
- Includes Korean/English UI layout and theme, F1/IME lifetime, settings cache,
  settings persistence, audio-only view, overlay paint and window geometry tests.
- GPU paths exercised VSR policy, HDR/SDR and overlay regressions; the reported
  GPU-labelled aggregate test duration was 18.11 seconds, not video latency.
- Audio coverage includes callbacks, output initialization, PCM/5.1 mixing,
  clock/jitter/stall replay and the default 25ms target replay.
- Added update discovery regression for v1.3.1 -> v2.0.0, rejection of older/equal
  releases and acceptance of v2.0.1. Passed independently and in the full suite.
- git diff --cached --check passed. Upstream font-license whitespace is preserved
  with a narrowly scoped .gitattributes rule; application whitespace checks remain.

## Packaging

- Runtime label, PE resource, CMake and installer versions aligned to 2.0.0.
- Installer and portable ZIP include the same production executable.
- SHA256 of production EXE:
  897226E3D07B22F39CEA73F46D2E8FA62FF9B5BA322C9BE7D01FBD57AF71F142.
- ZIP entries and Inno Setup payload manifest checked against source hashes.
- GPL, ASIO SDK/host and Pretendard OFL license notices included.
- Embedded fonts match hashes documented in third_party/pretendard/SOURCE.txt.
- User settings/logs, generated community drafts, previews and test binaries
  excluded from release assets and the source commit.
- Local Defender custom scan of packaged output completed with no threats found.
  This is a local result, not a guarantee about other signatures or vendors.

## Limits

No new physical capture-card long-duration run, HDMI-to-display latency
measurement or clean-machine installation/upgrade run was performed in this
release pass. Automated tests are not proof of universal hardware compatibility.
VSR request success is not proof that NVIDIA enabled the effect; it can add GPU
work and latency. HDR10, LPCM 5.1, ASIO and VSR retain their experimental labels.

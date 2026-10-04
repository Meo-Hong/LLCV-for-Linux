# Native HDR10 + NVIDIA VSR (development build)

This change is not in the published v2.0.0 assets. No release or driver-profile
change is part of this implementation.

## Processing and latency policy

- NVIDIA NV12 SDR and P010 HDR10 routes can request VSR at native size or larger.
  Non-NVIDIA adapters, YUY2 and either-axis downscaling remain bypassed.
- Native HDR stays P010 -> R10G10B10A2 BT.2020/PQ on the existing video processor
  and HDR swap chain. No SDR intermediate, tone map or RTX Video HDR extension
  is introduced. Stream chroma placement and limited-range decoding are retained.
- F6 changes the request on the render thread without rebuilding capture,
  processor, swap chain or upload ring. No new surfaces, frame queue, CPU readback,
  GPU query, wait or frame timing instrumentation is added to normal playback.
- A failed VSR-enabled Blt retries the same frame once after a confirmed OFF.
  Rejection stays latched until a request change/rebuild. Failed OFF is reported
  as unknown; failed fallback and device-loss errors still propagate normally.
- Driver processing can still increase GPU work and actual display latency.
  No new end-to-end latency claim is made. Screenshots remain source-derived.

NVIDIA's [RTX Video FAQ](https://nvidia.custhelp.com/app/answers/detail/a_id/5448/)
documents HDR VSR upscaling starting with its January 2025 update. Actual
activation/quality remains driver-dependent; accepted requests alone are not proof.

## Validation on 2026-10-04

Host: RTX 3080, NVIDIA driver 616.92. Clean x64 Release build; the application's
three private diagnostic options OFF. GPU readback/pacing below exists only in
test executables. No capture/audio device, visible test window or driver-setting
modification was used.

- Focused suite: 8/8 passed (41.27 s): screenshot, HDR policy, VSR policy/fault
  injection/live SDR controls, settings controller, HDR pipeline, new HDR VSR
  pipeline, SDR pipeline, video color.
- UI/transition suite: 8/8 passed (104.03 s): settings layout/text, both themes,
  overlay rendering, latest-frame mailbox, monitor moves, output transition state,
  transition faults and transition stress.
- New HDR GPU matrix: 720p/1080p/1440p inputs x 720p/1080p/1440p/4K output x both
  left/top-left chroma placements. 144 eligible ON samples accepted; 48 downscaled
  ON samples bypassed. Neutral PQ patches from black through 4000 nits and color
  patches stayed within 4/1023 codes of reference. F6 OFF restored exact pixels.
  Ten-bit formats, PQ/BT.2020 stream/output spaces and resource identity checked.
- Fine-detail pattern output differed between OFF/ON in all 18 eligible matrix
  cases, and was unchanged in the six downscaled cases. Every OFF restoration
  was byte-identical. No D3D debug-layer warnings/errors.
- Initially, very short cold synthetic tests showed no OFF/ON difference in
  either SDR or HDR despite accepted requests. User confirmed VSR enabled.
  A paced SDR control (360 frames per phase at 60 fps) showed repeatable ON/OFF
  differences; the following matrix run above also showed differences. This is
  consistent with asynchronous driver/model warm-up, not evidence of a disabled
  global setting. No additional wait is imposed on the viewer for warm-up.
- Separate `HdrPipelineTests --vsr-hdr-paced`: moving P010 720p -> 1440p,
  360 frames per OFF/ON/OFF phase at 60 fps, test-only synchronized completion.
  ON output differed; final OFF matched baseline exactly. This is evidence of
  request-dependent processing, not independent identification of NVIDIA's model
  or a physical latency measurement.
- Deterministic failure tests cover successful first Blt (no extra driver call),
  same-frame OFF retry, rejection latching, failed OFF, failed retry and device
  removal/reset/hang propagation.

Logs: `outputs/vsr-hdr/ctest-focused.log`, `ctest-focused-detail.log`,
`ctest-ui-transition.log`, `sdr-paced.log`, `hdr-paced.log`.

Not verified: physical HDMI capture-to-panel latency, live console HDR appearance
on the HDR monitor, or every NVIDIA GPU/driver/quality setting. The hidden GPU
fixture's monitor reported SDR; this does not alter the tested native PQ buffer,
but it must not be described as an end-to-end HDR-display test. Windows HDR and
an HDR-capable output are still required for live HDR viewing.

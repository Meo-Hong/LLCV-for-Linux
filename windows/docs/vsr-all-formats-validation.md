# VSR formats and 120fps investigation — development build

Date: 2026-10-04. Not a released version or a claim of zero end-to-end latency.

## Implementation

- NV12 SDR: existing native video-processor path.
- YUY2 SDR: now eligible on NVIDIA using the same native processor/input view.
  No app-side NV12 conversion, extra pass, readback, or additional frame queue.
- MJPEG: existing decoder produces NV12, which uses the same eligible route.
- P010 HDR10: existing development P010 → RGB10 BT.2020/PQ route, without an SDR
  intermediate. Auto follows the negotiated format, not the selection label.
- Both displayed dimensions must still be at least the source dimensions.
  Non-NVIDIA, downscaled and invalid routes remain bypassed.
- Existing request/Blt failure handling is retained. A rejected enhanced Blt
  retries the same frame once with VSR OFF; device-loss errors use normal recovery.
- HUD and Tab now say ON/OFF without “requested”. ON describes the enabled
  viewer feature, not a detector for NVIDIA's global activation. The setup guide
  still explains the NVIDIA active indicator; unavailable, rejected and unknown
  states are not shown as normal ON.

## GPU evidence

RTX 3080, NVIDIA 616.92. Native YUY2 1920×1080 → 2560×1440 ON/OFF produced
repeatable differences in 89.4771% of output pixels on a synthetic moving pattern.
ON and OFF hashes each matched the equivalent NV12 pattern exactly; repeated
OFF output was identical. This tests the native YUY2 route, not just S_OK from an
extension call. See outputs/vsr-hdr/fhd-qhd-120-yuy2-native.log.

The initial synthetic benchmark generated its pattern inside the paced loop.
Its serialized completion timings are diagnostic only, **not guaranteed 120fps
cadence or display latency**. The harness now precomputes moving input outside
the loop. Serialized readback exists only in the test executable.

The final precomputed-color YUY2 1080p → 1080p benchmark also changed 94.9941%
of pixels with ON, with exact repeatability for each ON/OFF output. Its measured
phase cadence was approximately 120fps (119.517–119.892fps in the last three
phases). Native-size processing therefore has pixel evidence too.
See outputs/vsr-hdr/yuy2-native-color-final.log. The appended native-controls
subtest uses NV12; the six benchmark phases themselves use YUY2.

## Actual capture

User authorized live testing. The private build overrides settings for that
session only: GC573, NV12 1920×1080 at 120fps, 2560×1440 output, smooth scaling,
AllowTearing. The ON and OFF runs use the same capture/display plan.
The user's INI is not saved or migrated.

| 60-second run | Tracked capture / Present returns | Replaced samples | Return interval mean / maximum |
| --- | --- | --- | --- |
| OFF | 6624 / 6624 | 0 | 8.333 / 12.299 ms |
| ON | 6629 / 6629 | 0 | 8.333 / 15.517 ms |

Counts exclude the initial monitoring warmup. They describe application capture
and successful Present calls, **not every physical scanout**. ON had an isolated
long return interval, so these results do not prove that the reported stutter
is gone. CPU call duration is not GPU completion or capture-to-photon latency.

- outputs/vsr-hdr/live-fhd120-qhd-off.log
- outputs/vsr-hdr/live-fhd120-qhd-on.log

A further ON run also recorded zero replaced frames and a 15.370 ms maximum
return interval. Its capture/Present totals differ by one at the asynchronous
monitoring boundary; this is not evidence of frame generation.

PresentMon display tracing failed with lost ETW events and produced no usable CSV.
Do not infer displayed FPS/drop counts from those attempts. A synthetic
wait-before-render comparison was also invalid (occluded output in the sandbox;
timeout in the alternate candidate on the desktop), and was not used to change
production scheduling. The temporary comparison harness was removed; its logs
remain in outputs/vsr-hdr/present-120-*.log for audit.

## Remaining limitation

No frame-drop cause has been established or certified fixed. Increasing frame
queue depth, silently disabling VSR, or changing the global NVIDIA quality was
not done. The renderer retains one latest capture sample and its existing
three upload surfaces. Rotation reduces contention but is not a GPU completion
fence. Further diagnosis needs a reproducible stuttering scene and valid
display-event evidence, rather than assuming GPU timestamps or CPU timings are
actual VSR latency.

The private LLCV_VSR_EXPERIMENT build adds bounded CPU upload/Present/interval
histograms and a --vsr-live-test on|off override used with --smoke-test-60.
Those clock calls and overrides are compiled out of the normal executable.

Relevant API background: [Microsoft waitable swap-chain guidance](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_3/nf-dxgi1_3-idxgiswapchain2-getframelatencywaitableobject).
No new wait policy is enabled based on documentation alone.

## Regression and handoff

- Release configuration, all private diagnostics OFF: 17/17 selected CTest cases
  passed in 143.43 seconds (settings/theme/controller, latest-frame ownership,
  NV12/YUY2 live toggles and OFF restoration, HDR/HDR+VSR/SDR color pipelines,
  HUD/Tab layout, window transitions/corners and audio-only UI).
- The earlier OverlayUi test expected the old “activation unverified” caption.
  It was updated to the requested concise ON label; setup-guide activation
  disclosure remains separately tested. The final run has no failures or skips.
- The user's settings.ini SHA-256 stayed
  A1B0BD03CF27C6343B95F00A30BA8DF90151CC70EF9C8DF681E49982EE0811CF
  before and after live testing.
- Normal test executable:
  outputs/vsr-hdr/LowLatencyCaptureViewer_AllFormats_Test.exe
- SHA-256:
  4EED465DA1DB8B856FE43FC5D34376D45BB0E6059AACB4C2C313042C3F1FC0C5
- A local Defender custom scan with remediation disabled returned no threats.
  This is the result for this binary/signature state, not a universal guarantee.
- No release, push or merge was performed.

## Preventive overlay work reduction (2026-10-04)

The render-thread overlay refresh previously rebuilt diagnostic and notification
text/layouts and painted both textures whenever any visible overlay needed a
new generation. Audio-only visibility therefore also refreshed the hidden Tab
panel and hidden transient HUD. The video-mode UI timer invalidates the shared
generation every 500 ms; interactions can invalidate it sooner.

Refresh now takes the actual visible-panel set and caches validity per panel
within each generation. Hidden panels perform no layout creation or D2D painting;
revealing a previously hidden panel refreshes it even in the same generation.
Cache validity is published only after successful drawing and cleared on reset.
This removes avoidable work, not the shared timer or visible meter updates.

Regression evidence:

- GPU-backed OverlayUiTests in both languages verify hidden panels allocate no
  layouts, 32 audio updates preserve hidden layout identities and texture bytes,
  visible audio pixels update, same-generation reveal refreshes stale panels, and
  selective/full notification and audio renders produce identical pixels.
- An intentionally nested D2D BeginDraw causes a drawing failure: the failed
  generation is not cached, and retry succeeds. Reset clears the validity mask.
- Nine selected tests passed (42.79 seconds): overlay UI, HDR, HDR+VSR, SDR,
  VSR toggles, latest-sample ownership and output/video transitions.
  The final added reset assertion passed in a further OverlayUiTests run.
- Normal Release executable, private diagnostics OFF:
  outputs/vsr-hdr/LowLatencyCaptureViewer_OverlayIsolation_Test.exe
- SHA-256:
  9527ECD8CE31C7833CF167F34130185E0D4C2FD560179EFC81393400FB43C465
- Local Defender custom scan with remediation disabled found no threats.

No additional video buffers, GPU waits, capture-format conversions, automatic
VSR quality reductions or scheduling changes were introduced. This change has
not been shown to explain or eliminate the previously observed 15.5 ms return
interval, and no physical latency improvement is claimed. No new live capture
or display-event tracing was performed for this follow-up.

## GPU-vendor UI gate

Startup identifies the default D3D11 rendering adapter using the renderer's
device-creation policy. It does not enumerate installed NVIDIA GPUs and assume
one will be used, request VSR, or certify RTX support. The temporary device is
released before settings/capture starts. This adds startup work only.

For a non-NVIDIA adapter, settings uncheck/disable VSR and its capture-size
selection, retain the setup guide, and explain that an NVIDIA rendering GPU is
required. An identification failure has a separate retry-on-restart message.
Stored ON cannot activate the split capture/display plan at immediate startup.
Stale checkbox notifications cannot enable VSR or trigger capability rescans.
The render device publishes its actual vendor on creation; F6 refuses to enable
VSR on non-NVIDIA/unknown adapters. Existing render-side eligibility checks remain.

Vendor gating is not a test of RTX model support, global driver settings, or
actual image enhancement. NVIDIA request rejection/fallback still handles
unsupported drivers/models. No GPU-selection option, driver-profile writes,
per-frame probing, extra buffering or GPU waits were added.

Validation: settings/view/theme/controller plus VSR, HDR, HDR+VSR, SDR and OSD
passed 8/8 CTests (140.93 s). After adding the stale capture-selection guard,
the six affected controller/renderer cases were rebuilt and passed again
(36.14 s). The UI tests cover reason text and gating through page switches in
Korean/English at 96/120/144/192 DPI. Intel/AMD/unknown vendor input is injected
for F6 tests; this is not physical Intel/AMD hardware validation. The host's
default-device probe returned NVIDIA (0x10DE). No GPU-selection UI was added.

Normal Release test executable: outputs/vsr-hdr/LowLatencyCaptureViewer_GpuGate_Test.exe
SHA-256: 78E6C54EE54110E6DFBD85F155FB69590995B419A1ACA44570EDC5FDA35E50D2
All private diagnostics flags OFF; local non-remediating Defender scan found
no threats. Logs: outputs/vsr-hdr/ctest-gpu-gate.log and ctest-gpu-gate-final.log.

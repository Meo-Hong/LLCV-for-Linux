# LLCV v2.1.1

## 한국어

### 녹화

- **MJPEG SDR 입력 녹화**를 추가했습니다. NV12·YUY2와 마찬가지로 지원되는 H.264·HEVC·AV1 코덱으로 MP4에 저장합니다.
- **녹화 음량**을 재생 음량과 별도로 조절할 수 있습니다. 녹화 탭의 오디오(AAC) 옆에서 시작 전에 0~100%로 설정하세요. 기본값은 100%, 0%는 무음입니다.
- 녹화 해상도·프레임 상한과 오디오 설정의 배치·간격을 정리했습니다.
- 녹화 시작·종료 안정성과 첫 녹화의 설정 적용을 보완하고, NV12·YUY2 녹화에서 입력의 색 범위·색 행렬 정보가 유지되도록 수정했습니다.

MJPEG 녹화는 추가 디코딩으로 CPU 사용량이 늘 수 있습니다. 현재 녹화는 **SDR 영상·16비트 PCM 스테레오 입력**만 지원하며, HDR·5.1·오디오 전용 녹화는 지원하지 않습니다. 코덱·해상도·프레임 지원 여부는 인코더에 따라 다릅니다.

### 목록에 없는 30fps 요청

- **영상 → 목록에 없는 FPS 요청**을 켜면 드라이버 목록에 없는 30fps를 프레임 목록에서 선택할 수 있습니다. 기본값은 꺼짐이며, QHD를 포함한 선택한 해상도·포맷에 적용합니다.
- 자동 인식 아래에 **직접 요청: 30fps**를 다른 색으로 구분합니다. 지원 확인 표시가 아니며, **시작** 시 드라이버가 거부하거나 다른 FPS로 적용하면 안내하고 시작을 중단합니다.

30fps를 쓰려면 40fps 캡처는 끄세요. 시작 후 **Tab → 실제 입력 FPS**도 확인하세요. 60fps 입력을 앱에서 30fps로 줄이는 기능은 아니며, 모든 캡처보드의 동작을 보장하지 않습니다.

기존 설치 경로·설정·단축키는 유지합니다. 기본 녹화 상한도 **1080p·60fps**로 유지하며, 이미 저장한 설정을 덮어쓰지 않습니다.

[녹화 안내](RECORDING.ko.md) · [프레임 설정](VIDEO.ko.md#목록에-없는-30fps-요청) · [사용 가이드](USAGE.ko.md)

## English

### Recording

- Added **MJPEG SDR input recording**, saved to MP4 through supported H.264, HEVC or AV1 encoders, alongside NV12/YUY2 input.
- Added **recording volume** independent of playback volume. Set it next to Audio (AAC) in the Recording tab before recording: 0–100%, default 100%; 0% mutes the recording.
- Refined spacing and placement of recording resolution/frame limits and audio controls.
- Improved recording start/stop reliability and first-session settings, and preserved input color range/matrix metadata in NV12/YUY2 recordings.

MJPEG recording adds decoding work and may increase CPU usage. Recording supports **SDR video and 16-bit PCM stereo input** only; HDR, multichannel and audio-only recording remain unsupported. Codec, resolution and frame-rate support depend on the encoder.

### Unlisted 30 fps requests

- Enable **Video → Allow unlisted FPS** to select 30 fps even when the driver does not list it. Off by default; applies to the selected resolution/format, including QHD.
- A separately colored **Direct request: 30 fps** line distinguishes it from detected modes. This is not a support confirmation. On **Start**, rejection or a different negotiated rate stops startup with a notice.

Turn off 40 fps capture before selecting 30 fps. After starting, check **Actual FPS** in Tab. This is not software conversion from 60 fps, and compatibility with every capture device is not guaranteed.

Existing installation paths, settings and shortcuts are retained. Default recording limits remain **1080p/60fps**; saved preferences are not overwritten.

[Recording guide](RECORDING.md) · [Frame-rate settings](VIDEO.md#unlisted-30-fps-requests) · [User guide](USAGE.md)

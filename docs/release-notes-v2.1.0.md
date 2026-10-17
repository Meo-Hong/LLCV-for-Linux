# LLCV v2.1.0

## 한국어

### SDR 녹화

- 영상과 스테레오 소리를 MP4로 녹화할 수 있습니다. 기본 단축키는 **F9**이며, 도움말·진단에서 변경할 수 있습니다.
- **녹화** 탭에서 H.264·HEVC·AV1, 인코더, 품질 기준(CQP) 또는 비트레이트 지정을 선택합니다. 지원이 확인된 항목만 선택할 수 있습니다.
- 녹화 해상도·프레임 상한의 기본값은 **1080p·60fps**입니다. AAC 비트레이트와 저장 폴더도 설정할 수 있으며, 일시정지·재개와 시간·용량 기준 자동 분할을 지원합니다.
- 화면 구석에 녹화 상태를 표시하며, 종료할 때 저장을 마무리합니다. 비정상 종료 시에는 이미 저장된 MP4 조각을 최대한 보존합니다.

현재 녹화는 **NV12·YUY2 SDR 영상과 16비트 PCM 스테레오 입력**을 지원합니다. MJPEG·HDR·5.1 녹화는 지원하지 않습니다. VSR·OSD·앱 음량 조절은 녹화에 반영되지 않습니다. 녹화 설정은 재생 설정과 별개지만, 인코더·GPU·저장 장치의 여유에 따라 부하가 달라집니다. 갑작스러운 종료 시 마지막 구간이나 저장되지 않은 데이터의 복구를 보장하지는 않습니다.

### 화면 표시

- **저지연 (독점 전체화면)**을 추가했습니다. 기존 **저지연 (일반 출력)**과 모두 저지연 방식이며, 독점 전체화면은 환경에 따라 표시 지연을 더 줄일 수 있습니다. Alt+Tab·설정 창 전환 시 독점을 해제하고 복귀합니다. 일반 출력은 기본값으로 유지합니다.
- VSR 사용 여부와 관계없이 **캡처 해상도와 표시 해상도**를 따로 선택할 수 있습니다.
- 보더리스·독점 전체화면에서도 선택한 영상 크기를 유지하고, 남는 영역은 검게 표시합니다. **창** 탭에서 영상 위치를 선택할 수 있습니다.
- 조건이 맞는 SDR 출력에서 중복 화면 지우기를 줄였습니다.

독점 전체화면은 다른 창을 함께 쓰는 데 제약이 있으며, 실제 지연 개선 폭은 환경에 따라 다릅니다. 기존 설치 경로·설정·단축키는 유지합니다.

| LLCV 표시 방식 | OBS 평균 지연 | LLCV 평균 지연 | OBS 대비 감소 |
| --- | ---: | ---: | ---: |
| 일반 창 | 32.9ms | 27.9ms | 5.0ms |
| 보더리스 전체화면 | 33.3ms | 27.6ms | 5.7ms |
| 독점 전체화면 | 33.3ms | **11.4ms** | **21.9ms** |

OBS 32.2.2는 일반 창 행에서 창 프로젝터, 전체화면 두 행에서 같은 전체화면 프로젝터 결과를 기준으로 비교했습니다. QHD 120fps 캡처·4K 144Hz 모니터, 창 크기 1280×720 조건입니다.

Windows 표시 이벤트 기준 참고치로, 물리적인 입력 지연 측정은 아닙니다. 일부 누락 프레임을 제외한 결과이며 환경에 따라 달라집니다. [측정 조건과 한계](VIDEO.ko.md#obs와-독점-전체화면-비교)

[녹화 안내](RECORDING.ko.md) · [영상·전체화면](VIDEO.ko.md) · [사용 가이드](USAGE.ko.md)

## English

### SDR recording

- Record video and stereo audio to MP4. The default shortcut is **F9**, configurable under Guide & logs.
- The **Recording** tab offers H.264, HEVC or AV1, encoder selection, and quality (CQP) or target-bitrate control. Only verified supported choices are enabled.
- Recording resolution/frame-rate limits default to **1080p/60fps**. Configure AAC bitrate and save folder as well. Pause/resume and automatic duration/size-based splitting are supported.
- A corner indicator shows recording status. Closing finalizes the file, and fragmented MP4 storage preserves already-written portions after an unexpected exit.

Recording currently supports **NV12/YUY2 SDR video and 16-bit PCM stereo input**. MJPEG, HDR and 5.1 recording are not supported. VSR, OSD and app volume changes are not recorded. Recording settings are separate from playback, but encoder, GPU and storage load still matters. Recovery of the final fragment or unwritten data is not guaranteed.

### Presentation

- Added **Low latency (exclusive fullscreen)** alongside **Low latency (standard)**. Both are low-latency modes; exclusive fullscreen may further reduce display latency depending on the system. It releases/restores exclusive output across Alt+Tab and settings dialogs. Standard remains the default.
- Capture and display resolutions can be selected independently of VSR.
- Borderless and exclusive fullscreen retain the selected video size with black margins. Video position is selectable in **Window** settings.
- Reduced redundant clearing for eligible SDR output.

Exclusive fullscreen limits simultaneous use of other windows; latency improvements vary by setup. Existing installation paths, settings and shortcuts are retained.

| LLCV presentation mode | OBS mean latency | LLCV mean latency | Reduction vs. OBS |
| --- | ---: | ---: | ---: |
| Windowed | 32.9ms | 27.9ms | 5.0ms |
| Borderless fullscreen | 33.3ms | 27.6ms | 5.7ms |
| Exclusive fullscreen | 33.3ms | **11.4ms** | **21.9ms** |

OBS 32.2.2 used a windowed preview projector for the windowed row; both fullscreen rows share the same fullscreen preview projector baseline. Conditions: QHD 120fps capture, a 4K 144Hz monitor and a 1280×720 window.

Reference Windows display-event estimates, not physical input-to-photon measurements. Missing frames were excluded, and results vary by setup. [Conditions and limitations](VIDEO.md#obs-and-exclusive-fullscreen-comparison)

[Recording guide](RECORDING.md) · [Video and fullscreen](VIDEO.md) · [User guide](USAGE.md)

<img align="right" src="docs/images/app-icon.png" width="64" height="64" alt="LLCV">

# LLCV - Low Latency Capture Viewer

HDMI 캡처보드로 들어오는 영상과 소리를 재생하는 Windows용 저지연 뷰어입니다.
OBS 없이 콘솔 게임 화면을 보거나, 오디오 전용 모드로 소리만 들을 수 있습니다.

**[다운로드](https://github.com/seria-aa/LowLatencyCaptureViewer/releases/latest)** · [사용 가이드](docs/USAGE.ko.md) · [변경 내역](docs/release-notes-v2.0.2.md) · [English](README.md)

<br>

| 영상 설정 | 오디오 설정 |
| --- | --- |
| [![영상 설정](docs/images/settings-video.png)](docs/images/settings-video.png) | [![오디오 설정](docs/images/settings-audio.png)](docs/images/settings-audio.png) |

<sub>2.0.2 설정 화면 예시입니다. 이미지를 누르면 원본 크기로 볼 수 있습니다.</sub>

<br>

## 설치 및 실행

Windows 10/11 x64와 캡처보드 드라이버가 필요합니다.
별도 코덱 팩·FFmpeg·Visual C++ 재배포 패키지는 설치하지 않아도 됩니다.

[최신 릴리스](https://github.com/seria-aa/LowLatencyCaptureViewer/releases/latest)에서 **Setup.exe**로 끝나는 설치 파일을 받으세요.
설치 없이 쓰려면 **x64.zip**을 풀고 `LowLatencyCaptureViewer.exe`를 실행하면 됩니다.

1. OBS 등 캡처보드를 사용 중인 다른 프로그램을 종료합니다.
2. LLCV에서 **캡처 장치**를 확인하고 **시작**을 누릅니다.
3. 소리가 나오지 않으면 **캡처 오디오 장치**를 확인하세요.

<kbd>F1</kbd> 도움말 · <kbd>F2</kbd> 설정 · <kbd>F6</kbd> VSR · <kbd>F11</kbd> 전체화면 · <kbd>F12</kbd> 스크린샷 · <kbd>Tab</kbd> 진단

<br>

## 지원 기능

- 최신 프레임 우선 표시, 저지연·VSync 출력
- NV12/YUY2, MJPEG, P010 HDR10 입력
- NVIDIA VSR (실험적), 캡처·표시 해상도 개별 설정
- WASAPI Shared·Exclusive, ASIO, 콘솔 LPCM 5.1, 오디오 전용 모드
- 입력 해상도 PNG 스크린샷과 클립보드 복사
- 다크·라이트 테마, 전체화면, 창 크기 고정, 음량 조절, 한국어·영어

HDR10은 호환 장치와 HDR 디스플레이가, ASIO는 전용 드라이버가 필요합니다.
LPCM 5.1은 다채널 캡처·Shared 출력 전용입니다.

VSR 동작은 GPU·드라이버에 따라 다르며 지연이 늘 수 있습니다. HDR 스크린샷은 SDR로 저장됩니다.
[기능별 지원 조건](docs/USAGE.ko.md#호환성-및-사용-조건)을 확인하세요.

<br>

## 문서

[전체 문서 목록](docs/INDEX.ko.md)에서 릴리스 노트와 개발·검증 기록도 확인할 수 있습니다.

- [사용 가이드](docs/USAGE.ko.md) — 설정, 단축키, VSR, 스크린샷
- [영상·HDR](docs/VIDEO.ko.md) / [오디오·5.1](docs/AUDIO.ko.md)
- [문제 해결](docs/TROUBLESHOOTING.ko.md) / [진단·로그](docs/DIAGNOSTICS.ko.md) / [소스 빌드](docs/BUILDING.ko.md)

Linux용은 별도로 관리되는 [LLCV for Linux](https://github.com/Meo-Hong/LLCV-for-Linux) 포크를 확인하세요.
설치 방법과 지원 범위, 문의는 해당 저장소에서 안내합니다.

---

Copyright (C) 2026 seria-aa. [GNU GPL v3.0 또는 이후 버전](LICENSE).

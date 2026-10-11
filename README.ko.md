# LLCV - Windows·리눅스용 저지연 캡처 뷰어

> [English](README.md)

LLCV는 HDMI 캡처 장치의 영상과 소리를 낮은 지연으로 보여주는 뷰어입니다.
오래된 프레임은 버리고 가장 최근 프레임을 표시합니다.

| 폴더 | 내용 |
| --- | --- |
| [`linux/`](linux/README.ko.md) | Meo-hong이 만든 리눅스판: V4L2 캡처, OpenGL, PipeWire 오디오, Wayland HDR10. Ubuntu, Debian, Raspberry Pi OS, Fedora, RHEL, Arch용 패키지 |
| [`windows/`](windows/README.ko.md) | [seria-aa](https://github.com/seria-aa/LowLatencyCaptureViewer)의 원본 Windows LLCV 2.0.2 (수정 없음): DirectShow 캡처, D3D11 출력, WASAPI·ASIO 오디오 |

## 리눅스

- 소개와 지원 환경: [linux/README.ko.md](linux/README.ko.md)
- 빌드와 설치: [linux/BUILDING.ko.md](linux/BUILDING.ko.md)
- 패키지 받기: [Releases](https://github.com/Meo-Hong/LLCV-for-Linux/releases)

## Windows

Windows 소스는 원본 프로젝트에 공개된 그대로 두었습니다. Windows판 다운로드와
문의는 [원본 저장소](https://github.com/seria-aa/LowLatencyCaptureViewer/releases/latest)를
이용하세요. 원본 프로젝트의 변경 사항은 `windows/`에 반영합니다.

## 라이선스

GPL-3.0-or-later ([LICENSE](LICENSE)). 원본 Windows LLCV는 seria-aa, 리눅스판은
Meo-hong이 만들었습니다. 함께 들어 있는 외부 구성 요소는 각 폴더에 적힌 자체
라이선스를 따릅니다.

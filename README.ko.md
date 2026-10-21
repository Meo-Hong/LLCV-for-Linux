# LLCV - Windows·리눅스용 저지연 캡처 뷰어

> [English](README.md)

LLCV는 HDMI 캡처 장치의 영상과 소리를 낮은 지연으로 보여주는 뷰어입니다.
오래된 프레임은 버리고 가장 최근 프레임을 표시합니다.

> **원본 LLCV와 별도로 관리됩니다.** 이 저장소는 Meo-hong이 운영합니다.
> `linux/`의 리눅스판은 원본 프로젝트가 아니라 이 저장소에서 개발·배포·지원하며,
> 원본 프로젝트의 공식 배포판이 아닙니다.
> [seria-aa](https://github.com/seria-aa/LowLatencyCaptureViewer)의 원본
> Windows LLCV는 참고용으로 `windows/`에 수정 없이 보관합니다.

| 폴더 | 내용 |
| --- | --- |
| [`linux/`](linux/README.ko.md) | Meo-hong이 만든 리눅스판: V4L2 캡처, OpenGL, PipeWire 오디오, Wayland HDR10, MP4 녹화. Ubuntu, Debian, Raspberry Pi OS, Fedora, Nobara, RHEL, Arch용 패키지 |
| [`windows/`](windows/README.ko.md) | seria-aa의 원본 Windows LLCV 2.1.4 (수정 없음): DirectShow 캡처, D3D11 출력, WASAPI·ASIO 오디오 |

## 리눅스

현재 릴리스: **2.1.4** ([Releases](https://github.com/Meo-Hong/LLCV-for-Linux/releases/latest)) ·
F2로 기본 설정 화면 열기, 녹화 끝부분 데이터 누락 수정
([릴리스 노트](linux/release-notes/v2.1.4.md))

| 파일 | 설치할 OS |
| --- | --- |
| `llcv_2.1.4_amd64.deb` | Ubuntu 22.04~26.04, Debian 12·13과 파생 배포판 |
| `llcv_2.1.4_arm64.deb` | Raspberry Pi OS 64비트, Debian 12·13 (arm64) |
| `llcv_2.1.4_raspberrypi-32bit_armhf.deb` | Raspberry Pi OS 32비트 (trixie) |
| `llcv-2.1.4-1.x86_64.rpm` | Fedora, Nobara, RHEL, Rocky Linux, AlmaLinux 9·10 |
| `llcv-2.1.4-1-x86_64.pkg.tar.zst` | Arch Linux, Manjaro, EndeavourOS |

- `.deb` 패키지는 APT 저장소 `https://meo-hong.github.io/LLCV-for-Linux/`로
  자동 업데이트됩니다.
- 실제 장비 검증: Ubuntu 26.04 + AVerMedia GC553Pro + NVIDIA GeForce RTX 2070.
  그 밖의 환경은 빌드와 설치 확인까지 했습니다.
- 소개, 지원 환경, 사용법: [linux/README.ko.md](linux/README.ko.md)
- 소스에서 빌드: [linux/BUILDING.ko.md](linux/BUILDING.ko.md)
- 리눅스판 문의: [Issues](https://github.com/Meo-Hong/LLCV-for-Linux/issues)

## Windows

Windows 소스는 원본 프로젝트에 공개된 그대로 두었고, 이 저장소에서 개발하지
않습니다. Windows판 다운로드와 문의는
[원본 저장소](https://github.com/seria-aa/LowLatencyCaptureViewer/releases/latest)를
이용하세요. 원본 프로젝트의 변경 사항은 `windows/`에 반영할 수 있습니다.

## 라이선스

GPL-3.0-or-later ([LICENSE](LICENSE)). 원본 Windows LLCV는 seria-aa, 리눅스판은
Meo-hong이 만들었습니다. 함께 들어 있는 외부 구성 요소는 각 폴더에 적힌 자체
라이선스를 따릅니다.

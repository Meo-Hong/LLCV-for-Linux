# LLCV 리눅스판

> [English](README.md)

LLCV는 USB(UVC/V4L2) HDMI 캡처 장치의 영상과 소리를 낮은 지연으로 표시하는
뷰어입니다. 이 폴더는 Windows용 LLCV 2.0.2를 리눅스용으로 다시 작성한 것입니다.
기준 장치는 AVerMedia Live Gamer ULTRA S GC553이고, 기준 데스크톱은
Ubuntu 26.04(GNOME, Wayland)입니다.

| 항목 | Windows판 | 리눅스판 |
| --- | --- | --- |
| 영상 캡처 | DirectShow | V4L2 mmap 버퍼, 최신 프레임 우선 |
| 화면 출력 | D3D11 / DXGI | OpenGL 3.3 (Wayland는 EGL, X11은 GLX) |
| 색 변환 | D3D11 Video Processor | GLSL 셰이더, BT.601/709/2020, Limited/Full |
| HDR10 | P010 + DXGI HDR10 | P010 + Wayland `wp_color_management_v1`, 또는 톤매핑 |
| MJPEG | Media Foundation | libjpeg-turbo (YUV 평면으로 디코딩) |
| 오디오 | WASAPI / ASIO | PipeWire 위의 SDL3 오디오 |
| UI | Win32 + Direct2D | Dear ImGui 1.92 (번들), Pretendard 글꼴 |
| 설정 | `%LOCALAPPDATA%` | `~/.config/llcv/settings.ini` |
| 로그 | `%LOCALAPPDATA%` | `~/.local/state/llcv/logs/` |
| 스크린샷 | 사진 폴더 | `$XDG_PICTURES_DIR/LLCV/` |

리눅스판에서 지원하지 않는 기능: NVIDIA VSR, WASAPI Exclusive, ASIO,
콘솔 LPCM 5.1, 제조사 톤매핑 요청.

## 지원 환경

| | Ubuntu 22.04 | Ubuntu 24.04 | Debian 13 / Ubuntu 26.04 | Arch Linux |
| --- | --- | --- | --- | --- |
| 패키지 | `.deb` | `.deb` | `.deb` (`debian/`) | `PKGBUILD` (`packaging/arch/`) |
| SDL3 | 번들 3.4.18 (정적 링크) | 번들 3.4.18 (정적 링크) | 시스템 3.2 이상 | 시스템 3.2 이상 |
| Dear ImGui | 번들 1.92.2b | 번들 1.92.2b | 번들 1.92.2b | 번들 1.92.2b |
| HDR 입력 (P010) | 리눅스 7.1 이상 필요 | 리눅스 7.1 이상 필요 | 리눅스 7.1 이상 필요 | 최신 커널이면 지원 |
| HDR 출력 | 컴포지터가 오래돼 불가 (GNOME 42) | 컴포지터가 오래돼 불가 (GNOME 46) | GNOME 48+ / KDE Plasma 6 | GNOME 48+ / KDE Plasma 6 |

| | Debian 12 (bookworm) | Debian 13 (trixie) |
| --- | --- | --- |
| 함께 쓰는 OS | LMDE 6, Raspberry Pi OS (bookworm) | Kali, Raspberry Pi OS (trixie) |
| 패키지 | `.deb` amd64, arm64 | `.deb` amd64, arm64, armhf |
| SDL3 | 번들 3.4.18, 정적 링크 | 시스템 3.2 이상 |
| HDR 입력 (P010) | 리눅스 7.1 이상 필요 | 리눅스 7.1 이상 필요 |
| HDR 출력 | 컴포지터가 오래돼 불가 (GNOME 43) | GNOME 48 / KDE Plasma 6 (라즈베리파이 데스크톱 제외) |

Linux Mint 21, Pop!_OS 22.04, Zorin OS 17, elementary OS 7은 Ubuntu 22.04
기반이라 Ubuntu 22.04 패키지를, Linux Mint 22, Pop!_OS 24.04, Zorin OS 18은
Ubuntu 24.04 패키지를 씁니다. 새 릴리스에서 만든 패키지는 옛 릴리스에 설치되지
않습니다. Raspberry Pi OS 64비트는 `arm64`, 32비트는 `armhf`
패키지를 씁니다(Raspberry Pi 2 이상, Pi Zero와 Pi 1은 미지원).

| | Fedora 43 이상, Nobara | RHEL / Rocky / AlmaLinux 9, 10 |
| --- | --- | --- |
| 패키지 | `.rpm` (`packaging/rpm/llcv.spec`) | `.rpm` (같은 spec) |
| SDL3 | 시스템 3.4 | 번들 3.4.18, 정적 링크 (EPEL에 SDL3 없음) |
| HDR 입력 (P010) | 리눅스 7.1 이상 필요 | P010을 지원하는 uvcvideo 필요 (EL 9/10 커널에는 없음) |
| HDR 출력 | GNOME 48+ / KDE Plasma 6 | 컴포지터가 오래돼 불가 (GNOME 40 / 47) |

HDR을 쓸 수 없는 환경이면 앱이 그 사실과 이유를 직접 알려줍니다. 이유는
설정 → 영상 → HDR, F1 도움말, Tab 진단, 그리고 HDR 재생을 시작할 때 뜨는
메시지에 표시됩니다. 이때 HDR 신호는 SDR로 변환해 보여줍니다.

| GPU 드라이버 | SDR | HDR10 패스스루 (Wayland) | 톤매핑 |
| --- | --- | --- | --- |
| AMD / Intel (Mesa) | 지원 | 지원 (10비트 EGL 버퍼) | 지원 |
| NVIDIA (독점) | 지원 | 드라이버가 Wayland에서 10비트 EGL 버퍼를 줄 때 | 지원 |
| Raspberry Pi 4/5 (VideoCore, Mesa v3d) | 지원 (OpenGL ES 3.0) | 불가 (라즈베리파이 데스크톱에 색 관리 없음) | 지원 |

데스크톱 OpenGL 3.3을 쓸 수 없으면 자동으로 OpenGL ES 3.0으로 실행합니다.
라즈베리파이에서는 MJPEG를 CPU로 디코딩하므로 1080p NV12나 YUY2를 권장합니다.
Pi 4에서 4K MJPEG는 너무 무겁습니다.

HDR 경로는 드라이버 자체의 색 관리 지원에 의존하지 않습니다. LLCV가 직접
`wp_color_management_v1`로 창에 HDR 정보를 붙이고, HDR 처리는 컴포지터가
합니다. 드라이버는 10비트 버퍼만 제공하면 됩니다. 10비트 버퍼가 없으면 자동
모드는 SDR 톤매핑으로 표시합니다.

NVIDIA를 Wayland에서 쓰려면 EGL Wayland 플랫폼 라이브러리가 필요합니다.
Debian/Ubuntu는 `libnvidia-egl-wayland1` 패키지에 있습니다. Arch는 `egl-wayland`
패키지에 있고, `nvidia-utils`가 이미 의존합니다.

## 빌드와 설치

```sh
git clone https://github.com/Meo-Hong/LLCV-for-Linux.git
cd LLCV-for-Linux/linux
./tools/install-build-deps.sh
./tools/build-package.sh
```

스크립트가 배포판을 알아내서 빌드 의존성을 설치하고, `.deb`, `.rpm` 또는 Arch
패키지를 만듭니다. 마지막 명령이 내 시스템에 맞는 설치 명령을 알려줍니다.
자세한 내용은 [BUILDING.ko.md](BUILDING.ko.md)에 있습니다.

- 배포판별로 쓰는 패키지
- Debian, Fedora/RHEL, Arch 계열 수동 빌드
- Docker로 다른 릴리스와 라즈베리파이(ARM)용 빌드
- 패키지 없이 빌드, CMake 옵션, 빌드 문제 해결

## HDR

1. **캡처.** HDR10은 P010(10비트)으로 들어옵니다. GC553은 P010을 2560 × 1440
   30fps, 1080p 60fps까지 제공합니다. 리눅스 7.0 이하 커널의 `uvcvideo`는
   P010을 인식하지 못합니다(커널 로그에 `Unknown video format 30313050-…`).
   리눅스 7.1 이상 커널이나 P010을 지원하는 uvcvideo DKMS 모듈을 쓰세요.
   장치가 P010을 제공하는데 커널이 숨기면 설정 화면에 경고가 표시됩니다.
2. **HDR 모니터 (Wayland).** GNOME 48 이상이나 KDE Plasma 6에서 디스플레이
   설정의 HDR을 켜면, 자동 모드가 HDR10을 그대로 내보냅니다.
3. **SDR 모니터와 X11.** HDR10을 Windows판 HDR 스크린샷과 같은 곡선으로 SDR
   톤매핑합니다. 기준 흰색은 바꿀 수 있고 기본값은 203 nit입니다.

HDR 입력의 스크린샷은 항상 SDR PNG로 저장됩니다.

드라이버에서 10비트 버퍼가 이상하게 동작하면 `llcv --sdr`로 실행하거나 앱
실행기의 "HDR 없이 시작"을 쓰세요. 그다음 **HDR 표시 방식 → 항상 SDR
톤매핑**을 선택하세요.

## Wayland와 X11

기본값은 Wayland입니다. X11(XWayland)을 쓰려면 다음 중 하나를 하세요.

- **앱 → 디스플레이 백엔드 → X11**을 선택합니다. 다음 실행부터 적용됩니다.
- `llcv --x11`로 실행합니다.
- 앱 실행기의 "X11 모드로 시작" 동작을 사용합니다.

Wayland에서는 컴포지터가 창 위치를 정합니다. 그래서 시작 모니터 설정은
전체화면에서만 적용되고, 마지막 창 위치는 복원되지 않습니다. X11 모드는 둘 다
복원하지만 HDR 출력은 할 수 없습니다.

## 메뉴 막대

재생 중에 **Alt**를 눌렀다 떼면 화면 위쪽에 메뉴 막대가 나타납니다. 보기,
오디오, 도구, 설정, 도움말 메뉴가 있어서 기능 키를 외우지 않아도 됩니다.
Alt나 Esc를 다시 누르거나 메뉴 밖을 클릭하면 닫힙니다.

## 단축키

| 키 | 동작 |
| --- | --- |
| Alt | 메뉴 막대 표시 / 숨기기 |
| F1 | 앱 정보 · 단축키 |
| F2 | 재생을 멈추고 설정 열기 |
| F3 | 오디오 미터 · 음량 |
| F5 | 원본 1:1 크기로 복원 |
| F11 / Alt+Enter | 전체화면 |
| F12 | 입력 해상도 PNG 스크린샷 |
| Tab | 실시간 진단 |
| Esc | 전체화면 해제 / 종료 |
| 마우스 휠 | 음량 ±5% |
| 더블 클릭 | 전체화면 전환 |

## 문제 해결

- **영상이 안 나옴.** OBS 등 같은 캡처 장치를 쓰는 앱을 종료하세요.
- **4K 60 fps.** GC553은 4K 60 fps를 MJPEG로만 제공합니다. 비압축 NV12는
  4K 30 fps, P010은 1440p 30 fps까지입니다.
- **HDR 소스에서 색이 물빠진 것처럼 보임.** 소스는 HDR을 보내는데 8비트
  형식으로 캡처하고 있는 경우입니다. P010을 선택하거나, **HDR10 강제**를
  쓰거나, 소스의 HDR을 끄세요.
- **소리가 안 나옴.** **오디오 → 캡처 오디오 장치**를 확인하세요. 자동 모드는
  영상과 같은 USB 장치의 오디오를 선택합니다.
- **소리가 끊김.** PCM 버퍼 목표를 5 ms씩 올려 보세요.
- **/dev/video\* 권한 오류.** 로컬 세션으로 로그인하거나 사용자를 `video`
  그룹에 추가하세요.

## 업데이트

패키지는 APT 업데이트 저장소에 올릴 준비가 되어 있지만, 서명 키와 주소를 원
제작자와 협의한 뒤 정하기로 해서 아직 배포하지 않았습니다. 배포 절차는
[packaging/README.ko.md](packaging/README.ko.md)에 있습니다. 배포용 패키지는
계열마다 가장 오래된 지원 릴리스에서 빌드해 새 릴리스까지 파일 하나로 쓰고,
파일 이름에는 배포판 이름을 넣지 않습니다.

## 라이선스

GPL-3.0-or-later. Dear ImGui는 MIT, Pretendard는 SIL Open Font License 1.1을
따릅니다.

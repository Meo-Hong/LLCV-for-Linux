# LLCV 리눅스판

> [English](README.md)

LLCV는 USB(UVC/V4L2) HDMI 캡처 장치의 영상과 소리를 낮은 지연으로 표시하는
뷰어입니다. 이 폴더는 [seria-aa](https://github.com/seria-aa/LowLatencyCaptureViewer)의
Windows용 LLCV 2.0.2를 Meo-hong이 리눅스용으로 다시 작성한 것입니다.
기준 장치는 AVerMedia Live Gamer ULTRA S GC553Pro이고, 기준 데스크톱은
Ubuntu 26.04(GNOME, Wayland)입니다.

> **원본 LLCV와 별도로 관리됩니다.** 리눅스판은 Meo-hong이
> [Meo-Hong/LLCV-for-Linux](https://github.com/Meo-Hong/LLCV-for-Linux)에서
> 개발·배포·지원합니다. 원본 프로젝트의 공식 배포판이 아니며, 원 제작자가
> 관리하지 않습니다. 리눅스판 문제는 원본 저장소가 아니라
> [이 저장소의 이슈](https://github.com/Meo-Hong/LLCV-for-Linux/issues)에
> 남겨 주세요.

| 항목 | Windows판 | 리눅스판 |
| --- | --- | --- |
| 영상 캡처 | DirectShow | V4L2 mmap 버퍼, 최신 프레임 우선 |
| 화면 출력 | D3D11 / DXGI | OpenGL 3.3 (Wayland는 EGL, X11은 GLX), 또는 OpenGL ES 3.0 |
| 색 변환 | D3D11 Video Processor | GLSL 셰이더, BT.601/709/2020, Limited/Full |
| HDR10 | P010 + DXGI HDR10 | P010 + Wayland `wp_color_management_v1`, 또는 톤매핑 |
| MJPEG | Media Foundation | libjpeg-turbo (YUV 평면으로 디코딩) |
| 오디오 | WASAPI / ASIO | PipeWire 위의 SDL3 오디오 |
| UI | Win32 + Direct2D | Dear ImGui 1.92 (번들), Pretendard 글꼴 |
| 설정 | `%LOCALAPPDATA%` | `~/.config/llcv/settings.ini` |
| 로그 | `%LOCALAPPDATA%` | `~/.local/state/llcv/logs/` |
| 스크린샷 | 사진 폴더 | `$XDG_PICTURES_DIR/LLCV/` |
| 업데이트 | 앱 안의 업데이트 확인 | APT 저장소 (`.deb`) |

리눅스판에서 지원하지 않는 기능: NVIDIA VSR, WASAPI Exclusive, ASIO,
콘솔 LPCM 5.1, 제조사 톤매핑 요청.

## 설치

[Releases](https://github.com/Meo-Hong/LLCV-for-Linux/releases/latest)에서 내
시스템에 맞는 파일을 받으세요.

| 파일 | 설치할 OS | 명령 |
| --- | --- | --- |
| `llcv_2.0.2_amd64.deb` | Ubuntu 22.04~26.04, Debian 12·13, Linux Mint, Pop!_OS, Zorin OS, elementary OS, LMDE, Kali | `sudo apt install ./llcv_2.0.2_amd64.deb` |
| `llcv_2.0.2_arm64.deb` | Raspberry Pi OS 64비트, Debian 12·13 (arm64) | `sudo apt install ./llcv_2.0.2_arm64.deb` |
| `llcv_2.0.2_raspberrypi-32bit_armhf.deb` | Raspberry Pi OS 32비트 (trixie) | `sudo apt install ./llcv_2.0.2_raspberrypi-32bit_armhf.deb` |
| `llcv-2.0.2-1.x86_64.rpm` | Fedora, Nobara, RHEL, Rocky Linux, AlmaLinux 9·10 | `sudo dnf install ./llcv-2.0.2-1.x86_64.rpm` |
| `llcv-2.0.2-1-x86_64.pkg.tar.zst` | Arch Linux, Manjaro, EndeavourOS | `sudo pacman -U ./llcv-2.0.2-1-x86_64.pkg.tar.zst` |

- `.deb`를 설치하면 LLCV APT 저장소도 함께 등록되어, 다음 버전부터는
  `apt upgrade`나 소프트웨어 업데이트로 받습니다. `.rpm`과 Arch는 Releases에서
  새 파일을 받아 설치하세요.
- 릴리스 페이지의 `SHA256SUMS`로 받은 파일을 확인할 수 있습니다:
  `sha256sum -c SHA256SUMS --ignore-missing`.
- 홈 폴더에서 설치할 때 나오는 "Download is performed unsandboxed as root"
  안내는 무시해도 됩니다.
- 삭제: `sudo apt remove llcv`(`sudo apt purge llcv`는 저장소 설정까지 삭제),
  `sudo dnf remove llcv`, `sudo pacman -R llcv`.

## 지원 환경

| 배포판 | 파일 | HDR 출력 | 검증 |
| --- | --- | --- | --- |
| Ubuntu 26.04 | amd64 `.deb` | 가능 (GNOME 48 이상) | **실제 장비에서 실행 확인**, APT 업데이트 확인 |
| Ubuntu 24.04, Linux Mint 22, Pop!_OS 24.04, Zorin OS 18 | amd64 `.deb` | 불가 (GNOME 46) | 빌드, 설치 확인 |
| Ubuntu 22.04, Linux Mint 21, Pop!_OS 22.04, Zorin OS 17, elementary OS 7 | amd64 `.deb` | 불가 (GNOME 42) | 빌드, 설치 확인 |
| Debian 13, Kali | amd64 `.deb` | 가능 (GNOME 48 / KDE Plasma 6) | 설치 확인 |
| Debian 12, LMDE 6 | amd64 `.deb` | 불가 (GNOME 43) | 설치 확인 |
| Raspberry Pi OS 64비트 (bookworm, trixie) | arm64 `.deb` | 불가 | 빌드 |
| Raspberry Pi OS 32비트 (trixie) | armhf `.deb` | 불가 | 빌드 |
| Fedora 43 이상, Nobara | `.rpm` | 가능 (GNOME 48 이상 / KDE Plasma 6) | 설치 확인 (Fedora 44) |
| RHEL, Rocky Linux, AlmaLinux 9·10 | `.rpm` | 불가 (GNOME 40 / 47) | 빌드, 설치 확인 |
| Arch Linux, Manjaro, EndeavourOS | `.pkg.tar.zst` | 가능 (GNOME 48 이상 / KDE Plasma 6) | 빌드 |

"설치 확인"은 패키지 관리자가 패키지와 의존성을 받아들이는지까지 확인했다는
뜻이며, 그 환경에서 앱을 실행해 보지는 않았습니다. HDR 출력은 Wayland 세션에서만
됩니다. Raspberry Pi OS 32비트는 Raspberry Pi 2 이상이 필요하며, Pi Zero와 Pi 1,
Raspberry Pi OS bookworm 32비트는 지원하지 않습니다.

| GPU | 드라이버 | 화면 출력 | HDR10 패스스루 (Wayland) | 검증 |
| --- | --- | --- | --- | --- |
| NVIDIA | 독점 드라이버 | OpenGL 3.3 | 드라이버가 10비트 EGL 버퍼를 줄 때 | **실제 장비 (GeForce RTX 2070)** |
| AMD | Mesa | OpenGL 3.3 | 지원 | 아직 |
| Intel | Mesa | OpenGL 3.3 | 지원 | 아직 |
| Raspberry Pi 4/5 | Mesa v3d | OpenGL ES 3.0 | 불가 (라즈베리파이 데스크톱에 색 관리 없음) | 아직 |

데스크톱 OpenGL 3.3을 쓸 수 없으면 자동으로 OpenGL ES 3.0으로 실행합니다.
라즈베리파이에서는 MJPEG를 CPU로 디코딩하므로 1080p NV12나 YUY2를 권장합니다.
Pi 4에서 4K MJPEG는 너무 무겁습니다.

HDR 경로는 드라이버 자체의 색 관리 지원에 의존하지 않습니다. LLCV가 직접
`wp_color_management_v1`로 창에 HDR 정보를 붙이고, HDR 처리는 컴포지터가
합니다. 드라이버는 10비트 버퍼만 제공하면 됩니다. 10비트 버퍼가 없으면 자동
모드는 SDR 톤매핑으로 표시합니다.

NVIDIA를 Wayland에서 쓰려면 EGL Wayland 플랫폼 라이브러리가 필요합니다.
Debian/Ubuntu는 `libnvidia-egl-wayland1` 패키지에 있습니다. Arch는 `egl-wayland`
패키지에 있고, `nvidia-utils`가 이미 의존합니다. Fedora와 EL은 `egl-wayland`
패키지이며, RPM Fusion이나 NVIDIA 저장소로 드라이버를 설치하면 함께 설치됩니다.

HDR을 쓸 수 없는 환경이면 앱이 그 사실과 이유를 직접 알려줍니다. 이유는
설정 → 영상 → HDR, F1 도움말, Tab 진단, 그리고 HDR 재생을 시작할 때 뜨는
메시지에 표시됩니다. 이때 HDR 신호는 SDR로 변환해 보여줍니다.

### 캡처 모드 (GC553Pro)

| 해상도 | NV12 (비압축) | MJPEG | P010 (HDR) |
| --- | --- | --- | --- |
| 1920 × 1080 | 최대 120 fps | 240 fps | 최대 60 fps |
| 2560 × 1440 | 최대 60 fps | 120 / 144 fps | 최대 30 fps |
| 3840 × 2160 | 최대 30 fps | 60 fps | 제공 안 함 |

설정 화면의 프레임레이트 목록은 선택한 해상도와 형식에 대해 장치가 알려주는
값입니다. P010에는 리눅스 7.1 이상 커널이 필요합니다([HDR](#hdr) 참고).

## 소스에서 빌드

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

1. **캡처.** HDR10은 P010(10비트)으로 들어옵니다. GC553Pro는 P010을
   2560 × 1440 30fps, 1080p 60fps까지 제공합니다. 리눅스 7.0 이하 커널의
   `uvcvideo`는 P010을 인식하지 못합니다(커널 로그에
   `Unknown video format 30313050-…`). 리눅스 7.1 이상 커널이나 P010을 지원하는
   uvcvideo DKMS 모듈을 쓰세요. 장치가 P010을 제공하는데 커널이 숨기면 설정
   화면에 경고가 표시됩니다.
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
- **프레임레이트가 60까지만 보임.** 장치가 높은 프레임레이트를 일부 해상도에서만
  제공합니다. 120 fps는 1920 × 1080(NV12), 120·144 fps는 2560 × 1440에서
  MJPEG로 고르세요. 소스도 그 주사율로 신호를 보내야 하고, VSync 출력은 모니터
  주사율을 넘지 못합니다.
- **4K 60 fps.** GC553Pro는 4K 60 fps를 MJPEG로만 제공합니다. 비압축
  NV12는 4K 30 fps, P010은 1440p 30 fps까지입니다.
- **HDR 소스에서 색이 물빠진 것처럼 보임.** 소스는 HDR을 보내는데 8비트
  형식으로 캡처하고 있는 경우입니다. P010을 선택하거나, **HDR10 강제**를
  쓰거나, 소스의 HDR을 끄세요.
- **소리가 안 나옴.** **오디오 → 캡처 오디오 장치**를 확인하세요. 자동 모드는
  영상과 같은 USB 장치의 오디오를 선택합니다.
- **소리가 끊김.** PCM 버퍼 목표를 5 ms씩 올려 보세요.
- **/dev/video\* 권한 오류.** 로컬 세션으로 로그인하거나 사용자를 `video`
  그룹에 추가하세요.

## 업데이트

`.deb` 패키지는 APT 저장소 `https://meo-hong.github.io/LLCV-for-Linux/`(스위트
`stable`)를 등록합니다. 저장소는 "LLCV Archive Signing Key
<busimh001@gmail.com>"로 서명되어 있고, 새 버전은 일반 시스템 업데이트와 함께
들어옵니다. 각 릴리스는 `linux-v*` 태그로 GitHub Actions가 빌드하며, 같은 과정에서
Releases 페이지에도 파일이 올라갑니다. 배포 절차는
[packaging/README.ko.md](packaging/README.ko.md)에 있습니다.

## 라이선스

GPL-3.0-or-later. 원본 Windows LLCV는 seria-aa, 리눅스판은 Meo-hong이
만들었습니다. Dear ImGui는 MIT, Pretendard는 SIL Open Font License 1.1을
따릅니다.

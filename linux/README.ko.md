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

| | Ubuntu 24.04 | Debian 13 / Ubuntu 26.04 | Arch Linux |
| --- | --- | --- | --- |
| 패키지 | `.deb` | `.deb` (`debian/`) | `PKGBUILD` (`packaging/arch/`) |
| SDL3 | 번들 3.4.18 (정적 링크) | 시스템 3.2 이상 | 시스템 3.2 이상 |
| Dear ImGui | 번들 1.92.2b | 번들 1.92.2b | 번들 1.92.2b |
| HDR 입력 (P010) | 리눅스 7.1 이상 필요 | 리눅스 7.1 이상 필요 | 최신 커널이면 지원 |
| HDR 출력 | 컴포지터가 오래돼 불가 (GNOME 46) | GNOME 48+ / KDE Plasma 6 | GNOME 48+ / KDE Plasma 6 |

HDR을 쓸 수 없는 환경이면 앱이 그 사실과 이유를 직접 알려줍니다. 이유는
설정 → 영상 → HDR, F1 도움말, Tab 진단, 그리고 HDR 재생을 시작할 때 뜨는
메시지에 표시됩니다. 이때 HDR 신호는 SDR로 변환해 보여줍니다.

| GPU 드라이버 | SDR | HDR10 패스스루 (Wayland) | 톤매핑 |
| --- | --- | --- | --- |
| AMD / Intel (Mesa) | 지원 | 지원 (10비트 EGL 버퍼) | 지원 |
| NVIDIA (독점) | 지원 | 드라이버가 Wayland에서 10비트 EGL 버퍼를 줄 때 | 지원 |

HDR 경로는 드라이버 자체의 색 관리 지원에 의존하지 않습니다. LLCV가 직접
`wp_color_management_v1`로 창에 HDR 정보를 붙이고, HDR 처리는 컴포지터가
합니다. 드라이버는 10비트 버퍼만 제공하면 됩니다. 10비트 버퍼가 없으면 자동
모드는 SDR 톤매핑으로 표시합니다.

NVIDIA를 Wayland에서 쓰려면 EGL Wayland 플랫폼 라이브러리가 필요합니다.
Debian/Ubuntu는 `libnvidia-egl-wayland1` 패키지에 있습니다. Arch는 `egl-wayland`
패키지에 있고, `nvidia-utils`가 이미 의존합니다.

## 빌드

### Debian 13 / Ubuntu 26.04

```sh
sudo apt install build-essential debhelper cmake pkg-config \
    libsdl3-dev libturbojpeg0-dev libpng-dev \
    libwayland-dev libwayland-bin wayland-protocols
./tools/build-deb.sh
sudo apt install ./dist/ubuntu-26.04/llcv_*.deb
```

### Ubuntu 24.04

Ubuntu 24.04에는 SDL3 패키지가 없어서, 고정된 SDL3 3.4.18 소스를 받아
(SHA256 확인) 앱에 정적으로 넣습니다. SDL3가 없으면 `tools/build-deb.sh`가
알아서 받고 `pkg.llcv.bundled-sdl3` 빌드 프로필로 빌드합니다.

```sh
sudo apt install build-essential debhelper dpkg-dev curl
sudo apt-get build-dep -P pkg.llcv.bundled-sdl3 ./
./tools/build-deb.sh
sudo apt install ./dist/ubuntu-24.04/llcv_*.deb
```

### Docker로 다른 릴리스용 빌드

`.deb`는 그 릴리스의 glibc에 묶이므로, 빌드한 릴리스(또는 그보다 새 릴리스)에만
설치됩니다. 어느 PC에서든 Ubuntu 24.04용 패키지를 만들려면:

```sh
./tools/build-deb-docker.sh ubuntu:24.04
```

결과물은 `dist/ubuntu-24.04/`에 생깁니다. `debian:13` 같은 다른 이미지도
쓸 수 있습니다.

### Arch Linux

```sh
sudo pacman -S --needed base-devel cmake sdl3 libjpeg-turbo libpng wayland wayland-protocols
cd packaging/arch
makepkg -si
```

PKGBUILD는 이 소스 트리를 그대로 빌드하므로 체크아웃 안에서 실행하세요.

### 패키지 없이 빌드 (모든 배포판)

```sh
cmake -S . -B build
cmake --build build -j
./build/llcv
```

CMake 옵션:

| 옵션 | 기본값 | 의미 |
| --- | --- | --- |
| `LLCV_SDL3` | `AUTO` | `SYSTEM`, `BUNDLED` (정적, 먼저 `tools/fetch-sdl3.sh` 실행), `AUTO` |
| `LLCV_USE_SYSTEM_IMGUI` | `OFF` | 번들 대신 시스템 Dear ImGui 1.92 이상 사용 |
| `LLCV_WAYLAND_COLOR_MANAGEMENT` | `AUTO` | HDR 출력 빌드 (`ON`이면 의존성이 없을 때 오류) |
| `LLCV_SOURCE_TREE_DATA` | `ON` | 소스 트리에서 글꼴·아이콘 찾기 (패키지에서는 끔) |

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
[packaging/README.ko.md](packaging/README.ko.md)에 있습니다. 패키지 버전에는
`2.0.2~ubuntu24.04`처럼 빌드한 릴리스 이름이 붙습니다.

## 라이선스

GPL-3.0-or-later. Dear ImGui는 MIT, Pretendard는 SIL Open Font License 1.1을
따릅니다.

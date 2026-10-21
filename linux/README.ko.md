# LLCV 리눅스판

> [English](README.md)

LLCV는 USB(UVC/V4L2) HDMI 캡처 장치의 영상과 소리를 낮은 지연으로 표시하는
뷰어입니다. 이 폴더는 [seria-aa](https://github.com/seria-aa/LowLatencyCaptureViewer)의
Windows용 LLCV 2.1.4를 Meo-hong이 리눅스용으로 다시 작성한 것입니다.
기준 장치는 AVerMedia Live Gamer ULTRA S GC553Pro이고, 기준 데스크톱은
Ubuntu 26.04(GNOME, Wayland)입니다.

> **원본 LLCV와 별도로 관리됩니다.** 리눅스판은 Meo-hong이
> [Meo-Hong/LLCV-for-Linux](https://github.com/Meo-Hong/LLCV-for-Linux)에서
> 개발·배포·지원합니다. 원본 프로젝트의 공식 배포판이 아니며, 원 제작자가
> 관리하지 않습니다. 리눅스판 문제는 원본 저장소가 아니라
> [이 저장소의 이슈](https://github.com/Meo-Hong/LLCV-for-Linux/issues)에
> 남겨 주세요.

## 2.1.4 새 기능

- **F2**는 기본 설정 화면(영상, 오디오 전용 모드에서는 오디오)을 엽니다.
  녹화 옵션은 그대로 녹화 탭에 있습니다.
- 녹화를 멈출 때 마지막 영상·오디오 데이터가 빠지지 않습니다.
- `settings.ini`의 잘못된 숫자값은 기본값으로 읽습니다.

전체 목록: [릴리스 노트](release-notes/v2.1.4.md). 2.1.3에서는 [녹화](#녹화)(F9),
[재생 음소거](#재생-음소거)(M), 목록에 없는 30fps 요청, 전체화면 영상 크기·위치가
추가되었습니다. [2.1.3 릴리스 노트](release-notes/v2.1.3.md)와
[2.1.3.1 릴리스 노트](release-notes/v2.1.3.1.md)를 보세요.

| 항목 | Windows판 | 리눅스판 |
| --- | --- | --- |
| 영상 캡처 | DirectShow | V4L2 mmap 버퍼, 최신 프레임 우선 |
| 화면 출력 | D3D11 / DXGI | OpenGL 3.3 (Wayland는 EGL, X11은 GLX), 또는 OpenGL ES 3.0 |
| 색 변환 | D3D11 Video Processor | GLSL 셰이더, BT.601/709/2020, Limited/Full |
| HDR10 | P010 + DXGI HDR10 | P010 + Wayland `wp_color_management_v1`, 또는 톤매핑 |
| MJPEG | Media Foundation | libjpeg-turbo (YUV 평면으로 디코딩) |
| 오디오 | WASAPI / ASIO | PipeWire 위의 SDL3 오디오 |
| UI | Win32 + Direct2D | Dear ImGui 1.92 (번들), Pretendard 글꼴, 한국어·영어·일본어·스페인어·브라질 포르투갈어 |
| 설정 | `%LOCALAPPDATA%` | `~/.config/llcv/settings.ini` |
| 로그 | `%LOCALAPPDATA%` | `~/.local/state/llcv/logs/` |
| 스크린샷 | 사진 폴더 | `$XDG_PICTURES_DIR/LLCV/` |
| 녹화 | Media Foundation | FFmpeg: NVENC, VA-API, x264, x265, SVT-AV1 |
| 녹화 파일 | 동영상 폴더 | `$XDG_VIDEOS_DIR/LLCV/` |
| 업데이트 | 앱 안의 업데이트 확인 | APT 저장소 (`.deb`) |

리눅스판에서 지원하지 않는 기능: NVIDIA VSR, WASAPI Exclusive, ASIO,
콘솔 LPCM 5.1, 독점 전체화면과 호환성(Blt) 표시 방식([전체화면](#전체화면) 참고),
창 가장자리 스냅과 둥근 모서리(컴포지터나 창 관리자가 담당), 앱 안의 업데이트
확인(패키지 관리자가 담당).

## 설치

[Releases](https://github.com/Meo-Hong/LLCV-for-Linux/releases/latest)에서 내
시스템에 맞는 파일을 받으세요.

| 파일 | 설치할 OS | 명령 |
| --- | --- | --- |
| `llcv_2.1.4_amd64.deb` | Ubuntu 22.04~26.04, Debian 12·13, Linux Mint, Pop!_OS, Zorin OS, elementary OS, LMDE, Kali | `sudo apt install ./llcv_2.1.4_amd64.deb` |
| `llcv_2.1.4_arm64.deb` | Raspberry Pi OS 64비트, Debian 12·13 (arm64) | `sudo apt install ./llcv_2.1.4_arm64.deb` |
| `llcv_2.1.4_raspberrypi-32bit_armhf.deb` | Raspberry Pi OS 32비트 (trixie) | `sudo apt install ./llcv_2.1.4_raspberrypi-32bit_armhf.deb` |
| `llcv-2.1.4-1.x86_64.rpm` | Fedora, Nobara, RHEL, Rocky Linux, AlmaLinux 9·10 | `sudo dnf install ./llcv-2.1.4-1.x86_64.rpm` |
| `llcv-2.1.4-1-x86_64.pkg.tar.zst` | Arch Linux, Manjaro, EndeavourOS | `sudo pacman -U ./llcv-2.1.4-1-x86_64.pkg.tar.zst` |

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
| Ubuntu 22.04, Linux Mint 21, Pop!_OS 22.04, Zorin OS 17, elementary OS 7 | amd64 `.deb` | 불가 (GNOME 42) | 빌드, 설치 확인, 녹화 확인 (컨테이너) |
| Debian 13, Kali | amd64 `.deb` | 가능 (GNOME 48 / KDE Plasma 6) | 설치 확인 |
| Debian 12, LMDE 6 | amd64 `.deb` | 불가 (GNOME 43) | 설치 확인 |
| Raspberry Pi OS 64비트 (bookworm, trixie) | arm64 `.deb` | 불가 | 빌드 |
| Raspberry Pi OS 32비트 (trixie) | armhf `.deb` | 불가 | 빌드 |
| Fedora 43 이상, Nobara | `.rpm` | 가능 (GNOME 48 이상 / KDE Plasma 6) | 빌드, 설치 확인, 녹화 확인 (Fedora 44, 컨테이너) |
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

### 40fps 캡처

**영상 → 캡처 → 40fps 캡처**는 기본 꺼짐입니다. 체크하면 프레임레이트 선택 대신
캡처 드라이버에 40fps를 요청하고, 해제하면 기존 선택으로 돌아갑니다.

- 장치가 알려준 목록에 40fps가 없어도 요청할 수 있습니다. 지원 여부는 장치,
  해상도, 픽셀 형식에 따라 다릅니다.
- **시작**할 때 드라이버가 실제로 40fps를 설정했는지 확인합니다. 거부하거나 다른
  속도로 설정하면 다른 FPS로 대체하지 않고 오류를 표시합니다. 이때는 옵션을
  해제하고 다시 시작하세요.
- 프레임을 보간하거나 게임을 40fps로 고정하는 기능이 아니며, VRR을 대체하지
  않습니다. 40fps 게임도 120 fps 캡처로 볼 수 있으므로 꼭 켤 필요는 없습니다.
- 실제 수신 속도는 Tab 진단 화면의 **실제 FPS**에서 확인하세요.

### 목록에 없는 30fps 요청

**영상 → 캡처 → 목록에 없는 FPS 요청 (30fps)**은 기본 꺼짐입니다. 켜면 선택한
해상도와 형식에서 드라이버 목록에 30fps가 없을 때, 프레임레이트 목록에
**30fps · 직접 요청**이 강조색으로 추가됩니다.

- 지원 확인이 아니라 직접 요청입니다. **시작**할 때 드라이버가 설정한 속도를
  확인하고, 30fps를 거부하거나 다른 속도로 적용하면 다른 FPS로 대체하지 않고
  안내와 함께 시작을 중단합니다.
- 40fps 캡처는 먼저 끄세요. 60fps 입력을 앱에서 30fps로 줄이는 기능이 아닙니다.
- 시작한 뒤 Tab 진단의 **실제 FPS**를 확인하세요.

## 녹화

**F9**를 누르면 재생 중인 영상과 캡처 오디오를 MP4로 녹화합니다. 다시 누르면
녹화를 끝내고 파일을 마무리합니다. 옵션은 **설정 → 녹화**에 있습니다.

- **입력.** NV12, YUY2, MJPEG, BGR24 SDR 영상과 16비트 스테레오 캡처 오디오.
  HDR 입력(P010 또는 HDR10 강제)은 녹화하지 않습니다. MJPEG는 녹화용으로 한 번
  더 디코딩하므로 CPU를 더 씁니다.
- **코덱과 인코더.** H.264, HEVC, AV1. **자동 하드웨어**는 NVIDIA NVENC,
  VA-API(AMD, Intel) 순서로 시도합니다. **CPU**는 시스템 FFmpeg의 x264, x265,
  SVT-AV1을 씁니다. 녹화 탭을 처음 열면 쓸 수 있는 인코더를 확인하고 나머지는
  선택할 수 없게 표시합니다. 지원하지 않는 조합은 코덱을 바꾸거나 CPU로 넘어가지
  않고 오류를 알립니다.
- **기본값.** H.264, 자동 하드웨어, 품질 기준(고정 QP 23), 1080p·60fps 상한,
  AAC 192 kbps, 녹화 음량 100%, 분할 꺼짐. 상한은 원본보다 해상도나 프레임을
  높이지 않고, 재생 설정도 바꾸지 않습니다.
- **품질.** 작은 파일(QP 26), 균형(QP 23), 높은 품질(QP 20). AV1은 같은 수준의
  양자화 값을 씁니다. 비트레이트 지정은 2~100 Mbps입니다.
- **녹화 음량**(0~100%)은 재생 음량, 재생 음소거와 별개입니다. 0%는 무음으로
  녹화합니다.
- **자동 분할**은 시간(15, 30, 60, 120분)이나 용량(1, 2, 4, 8, 16 GiB) 기준입니다.
  다음 키프레임에서 새 파일을 시작하므로 기준을 조금 넘을 수 있습니다.
- **파일.** `$XDG_VIDEOS_DIR/LLCV/`(보통 `~/동영상/LLCV` 또는 `~/Videos/LLCV`)에
  `LLCV_YYYYMMDD_HHMMSS.mp4`로 저장하고, 분할된 파일은 `_part02`, `_part03` 식으로
  이어집니다. 녹화 탭에서 다른 폴더를 고를 수 있습니다. 파일은 fragmented MP4라서
  비정상 종료 때도 이미 기록된 부분은 대개 남지만, 마지막 몇 초는 보장하지
  않습니다.
- **일시정지·재개**는 메뉴 막대(**Alt → 녹화**)에 있습니다. 재생은 계속되고,
  일시정지한 시간은 파일에서 빠집니다.
- 화면 구석에 녹화 시간과 용량을 표시합니다. 이 표시와 다른 오버레이, 재생 음량,
  재생 음소거는 녹화에 들어가지 않습니다.
- 녹화 중에 설정을 열거나 종료하면 먼저 확인한 뒤 녹화를 끝내고 저장합니다.
  저장 공간 부족, 쓰기 오류, 인코딩 처리 지연이 생기면 안내와 함께 녹화를
  중단하며, 재생은 계속됩니다.

`.deb` 패키지에는 최소 구성의 FFmpeg 7.1.1이 들어 있어서 지원하는 모든 배포판에서
동작합니다. CPU로는 H.264(x264)를 녹화할 수 있고, HEVC와 AV1은 NVENC나 VA-API가
필요합니다. Raspberry Pi OS 32비트 패키지에는 x264가 없어서(배포판의 정적 x264를
링크할 수 없음) 녹화를 쓸 수 없습니다. Fedora `.rpm`과 Arch 패키지는 시스템 FFmpeg를 씁니다(빌드할 때 4.4 이상).
RHEL, Rocky Linux, AlmaLinux용 `.rpm`은 FFmpeg 없이 빌드하므로 녹화를 쓸 수 없다고
표시합니다.

### 녹화 테스트 결과 (2.1.3)

2.1.3 배포 전과 2.1.3.1에서 다시, 앱과 같은 녹화·캡처·설정 코드를 그대로 링크한 테스트 프로그램으로
녹화를 확인했습니다. 컨테이너에는 GPU가 없어서 CPU 인코더로 확인했습니다.

| 환경 | 확인한 내용 | 결과 |
| --- | --- | --- |
| Ubuntu 26.04, GeForce RTX 2070, FFmpeg 8.0 | H.264·HEVC(NVENC와 CPU), AV1(CPU), 품질 기준·비트레이트, NV12·YUY2·MJPEG·BGR24 입력, 일시정지, 1440p 120fps → 1080p 60fps 상한, 녹화 음량 0%, 소리 없음, 1분 분할 | 모든 파일 저장·디코딩 정상. 일시정지 구간은 빠지고, 둘째 파일은 키프레임에서 시작 |
| Ubuntu 26.04, GC553Pro | NV12 1080p 60fps, MJPEG 240fps 캡처 (HDMI 소스 없이 장치 테스트 화면) | 누락 없이 60fps로 녹화, 색 범위·행렬 유지 |
| Ubuntu 26.04, GC553Pro | 목록에 30fps가 없는 모드에서 30fps 직접 요청 | 드라이버가 59.94fps로 적용하면 안내 후 중단, 29.97fps는 허용 |
| Ubuntu 26.04 | 2.0.5 설정(F9나 M을 이미 사용) 불러오기, 새 옵션 저장·불러오기 | 기존 키 유지, 녹화·음소거에는 빈 키 배정 |
| Ubuntu 22.04 컨테이너, FFmpeg 4.4 | `.deb` 빌드·설치·실행, x264 두 모드 녹화 | 통과 |
| Fedora 44 컨테이너, `ffmpeg-free` 8.1 | `.rpm` 빌드·설치·실행, SVT-AV1 녹화 | 통과. `ffmpeg-free`에는 x264, x265 없음 |
| Fedora 44 컨테이너 + RPM Fusion FFmpeg (Nobara 구성) | 같은 `.rpm`, x264·x265·SVT-AV1 두 모드 | 통과 |
| FFmpeg 없는 빌드 (RHEL 계열) | 컴파일 | 통과 |
| 2.1.3.1: Ubuntu 22.04·24.04·26.04, Debian 12·13 컨테이너 | 22.04에서 빌드한 amd64 `.deb` 하나를 각각 설치·실행 | 통과. FFmpeg와 x264는 정적으로 들어 있고 시스템에서는 libva만 씀 |
| 2.1.3.1: Ubuntu 26.04, GeForce RTX 2070, `.deb`에 넣은 FFmpeg 7.1.1 | NVENC H.264·HEVC, x264 녹화 | 통과, 누락 프레임 없음 |
| 2.1.3.1: arm64·armhf `.deb` | 크로스 빌드 | FFmpeg 의존성 없이 통과(armhf는 x264·NVENC 없이 VA-API만). 설치 확인은 CI에서 QEMU로 실행 |

아직 확인하지 못한 것: AMD·Intel의 VA-API 인코딩, 다른 배포판의 NVENC, 실제 HDMI
소스의 소리 레벨과 A/V 싱크, 녹화 UI(녹화 탭, 녹화 표시, 확인 창). 녹화 UI는 빌드만
하고 이 테스트에서 조작해 보지는 않았습니다. 장치 내부 톤매핑 요청([HDR](#hdr) 참고)도
실제 장치로는 확인하지 못했습니다. GC553Pro에는 해당 제어가 없어서, HID 리포트 구성만
Windows판과 비교했습니다.

## 재생 음소거

**M**을 누르면 음량은 그대로 두고 재생 소리만 끕니다. 오디오 OSD(F3)와 오디오
전용 화면의 스피커 버튼, **Alt → 오디오 → 재생 음소거**도 같은 동작입니다.
오디오 OSD를 닫아 두면 영상 우측 상단에 반투명 스피커 아이콘이 남습니다.

- 녹화 소리는 음소거되지 않습니다.
- 마우스 휠이나 메뉴의 음량 올리기·내리기를 쓰면 음소거가 풀립니다.
- 앱을 종료하면 초기화되며, **백그라운드에서 자동 음소거**와 따로 동작합니다.

## 음량 조절과 오디오 전용 화면

- 오디오 OSD(F3)나 오디오 전용 화면에서 전체·왼쪽·오른쪽 음량 조절을 더블클릭하면
  100%로 돌아갑니다. 이때 재생 음소거도 풀립니다.
- 왼쪽·오른쪽 조절 위에서 마우스 휠을 돌리면 그 채널만 바뀝니다. 전체 음량 조절이나
  그 밖의 곳에서는 전체 음량이 바뀝니다.
- **볼륨 증폭**을 켜고 100%를 넘기면 주황색으로 표시합니다.
- 오디오 전용 화면은 조절 영역 밖을 드래그하면 창이 움직이고, 크기를 바꿔도 비율이
  유지됩니다. **창 → 테두리 없는 창**을 켜면 오디오 전용 화면의 제목 표시줄도 숨기고,
  가장자리를 드래그해 크기를 바꿉니다.

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

**영상 → HDR → HDR 크로마 위치**는 P010 입력의 색 경계가 어긋나 보이지 않으면
**자동**으로 두세요. **좌측**과 **좌측 상단**은 크로마 샘플 위치가 다른 장치를 위한
호환용 설정입니다.

LLCV는 시작할 때마다 지원하는 캡처 장치에, 8비트 형식(NV12, YUY2, MJPEG, BGR24)에서는
장치가 직접 HDR을 SDR로 톤매핑하고 P010이나 **HDR10 강제**에서는 HDR을 그대로 보내도록
Windows판과 같은 요청을 보냅니다. 대상은 AVerMedia UVC 확장 제어가 있는 AVerMedia
장치와 Elgato HD60 S+, HD60 X, HD60 X Rev.2입니다. 패키지는 로그인한 사용자가 Elgato
HID 인터페이스에 접근할 수 있도록 udev 규칙을 설치합니다. 설치 후 장치를 한 번 다시
연결하세요. GC553Pro에는 해당 제어가
없어서 아무것도 보내지 않습니다. 장치가 요청을 받아들여도 입력 형식이 실제로 바뀌었다는
보장은 아니며, 결과는 진단 로그의 `[capture-hdr]` 줄에 남습니다.

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

## 전체화면

**창 → 전체화면 영상 크기**에서 **화면에 맞춤**(기본값)이나 **원본 크기**를
고릅니다. 원본 크기는 캡처 영상을 1:1로 보여주고, 화면보다 클 때만 줄입니다.
**창 → 전체화면 영상 위치**는 영상이 화면을 다 채우지 않을 때(예: 울트라와이드
모니터의 16:9 영상) 중앙, 가장자리, 모서리 중 어디에 둘지 정합니다. 남는 영역은
검게 표시하고, 영상은 잘리지 않습니다.

캡처 해상도가 모니터와 같으면(예: 1440p 모니터에서 2560 × 1440 캡처) 전체화면으로
재생을 시작하고, **F5**도 전체화면을 씁니다. 그래야 픽셀이 1:1로 보이기 때문입니다.
그 밖에는 F5가 1:1 크기 창으로 돌아가며, 모니터에 맞추려고 창을 줄여야 하면 그렇다고
알려 줍니다.

**창 → 모니터를 옮길 때 상대적인 창 크기 유지**(기본 꺼짐)를 켜면, 크기가 다른 모니터로
창을 옮겨도 화면에서 차지하는 비율이 비슷하게 유지됩니다. 정확한 1:1이 필요하면 옮긴 뒤
F5를 누르세요.

Windows판의 독점 전체화면 방식은 리눅스판에 없습니다. Wayland 컴포지터는
전체화면 창을 합성 없이 바로 표시할 수 있고, X11에서는 SDL이 컴포지터에
전체화면 창을 합성하지 않도록 요청합니다.

## 재생 중 절전 방지

**앱 → 전원 → 재생 중 자동 절전 방지**는 기본 켜짐입니다.

- 영상 재생 중에는 화면과 시스템을 깨워 둡니다.
- 오디오 전용 모드에서는 화면 꺼짐을 허용하고, 시스템 자동 절전만 막습니다.
- 재생을 멈추거나 캡처 장치 연결이 끊기면 해제합니다. 설정 화면에서는 화면
  보호기와 화면 꺼짐을 막지 않습니다.
- 시스템 전원 설정은 바꾸지 않습니다.

화면은 SDL을 통해 Wayland idle-inhibit 또는 화면 보호기 D-Bus 인터페이스로
유지합니다. 시스템 자동 절전은 데스크톱 포털(`org.freedesktop.portal.Inhibit`)로
막으므로 `xdg-desktop-portal`이 실행 중이어야 합니다(GNOME, KDE Plasma 등).
포털이 없으면 진단 로그에 그 사실을 남깁니다.

## 메뉴 막대

재생 중에 **Alt**를 눌렀다 떼면 화면 위쪽에 메뉴 막대가 나타납니다. 보기,
오디오, 녹화, 도구, 설정, 도움말 메뉴가 있어서 기능 키를 외우지 않아도 됩니다.
**녹화 → 마지막 녹화 열기**는 가장 최근 파일을 기본 플레이어로 엽니다.

재생 중에 연 설정 화면(F2나 메뉴)은 영상 화면(오디오 전용 모드에서는 오디오 화면)부터
보여 줍니다. **취소** 버튼이 있어서, 변경 사항을 버리고 이전
설정으로 재생을 다시 시작할 수 있습니다.
Alt나 종료 키(기본 Esc)를 다시 누르거나 메뉴 밖을 클릭하면 닫힙니다.

## 단축키

아래 키는 기본값입니다. **설정 → 안내 · 로그**에서 바꿀 수 있습니다.

| 기본 키 | 동작 |
| --- | --- |
| Alt | 메뉴 막대 표시 / 숨기기 |
| F1 | 앱 정보 · 단축키 |
| F2 | 재생을 멈추고 설정 열기 (영상 화면, 오디오 전용 모드에서는 오디오 화면) |
| F3 | 오디오 미터 · 음량 |
| F5 | 원본 1:1 크기로 복원 (캡처 해상도가 모니터와 같으면 전체화면) |
| F11 / Alt+Enter | 전체화면 |
| F12 | 입력 해상도 PNG 스크린샷 |
| Tab | 실시간 진단 |
| Esc | 전체화면 해제 / 종료 |
| F9 | 녹화 시작 / 종료 |
| M | 재생 음소거 켜기 / 끄기 |
| 마우스 휠 | 음량 ±5% (음소거도 해제) |
| 더블 클릭 | 전체화면 전환 |

### 단축키 변경

1. **설정 → 안내 · 로그**에서 동작 옆의 키 버튼을 누르세요.
2. 원하는 키를 누르세요. **Ctrl**, **Alt**, **Shift** 조합도 됩니다. 같은
   버튼을 다시 누르면 취소됩니다.
3. **시작**을 누르세요. 바꾼 키는 다른 설정과 함께 저장됩니다.

**전체화면 해제 / 종료**(Esc)를 포함한 10개 단축키를 모두 바꿀 수 있습니다.
빈 단축키는 둘 수 없습니다. 중복 키, 수정 키만 누른 경우, Super(Windows 키)
조합, Alt+F4, Alt+Tab, Ctrl+Alt+Delete, Ctrl+Alt+F1~F12 같은 예약 조합은
지정할 수 없습니다. **기본값 복원**은 10개를 모두 처음 키로 되돌립니다.
Alt(메뉴 막대)와 Alt+Enter(전체화면)는 고정입니다.

이전 버전 설정에서 F9나 M을 이미 다른 동작에 쓰고 있으면 그 키는 그대로 두고,
녹화 시작 / 종료나 재생 음소거에는 비어 있는 기능 키를 배정합니다. 배정된 키는
**안내 · 로그**에서 확인하세요.

키는 물리적인 위치로 구분하므로 한글 입력기가 켜져 있어도 동작합니다. 도움말,
메뉴 막대, 오버레이, 설정 화면에 지정한 키가 표시됩니다.

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
- **40fps 캡처가 시작되지 않음.** 이 장치, 해상도, 형식에서 드라이버가 40fps를
  거부했거나 다른 속도로 설정한 경우입니다. 오류 내용과 진단 로그의
  `40 fps request` 줄을 확인하고, 다른 해상도나 형식을 고르거나 **40fps 캡처**를
  해제하세요.
- **녹화를 쓸 수 없거나 선택지가 흐리게 보임.** FFmpeg 없이 빌드한 패키지(RHEL
  계열)이거나, 고른 코덱에 쓸 수 있는 인코더가 없는 경우입니다. `.deb` 패키지에서
  CPU 녹화는 H.264만 됩니다. 하드웨어 인코딩은
  VA-API 드라이버(Intel은 `intel-media-va-driver`, AMD는 `mesa-va-drivers`)나
  NVIDIA 드라이버를 설치하세요. 아니면 **CPU**를 고르세요. 인코더 확인 결과는
  진단 로그에 남습니다. Fedora 기본 `ffmpeg-free`에는 x264와 x265가 없어서 CPU로
  H.264, HEVC를 녹화할 수 없습니다. SVT-AV1과 하드웨어 인코더는 됩니다. RPM
  Fusion의 FFmpeg로 바꾸면(`sudo dnf swap ffmpeg-free ffmpeg --allowerasing`) 쓸 수
  있고, Nobara에는 처음부터 들어 있습니다. AMD에서 H.264, HEVC를 쓰려면 Fedora에서도
  RPM Fusion의 `mesa-va-drivers-freeworld`가 필요합니다.
- **"처리가 밀려" 녹화가 멈춤.** **해상도 상한**이나 **프레임 상한**을 낮추거나
  하드웨어 인코더를 쓰세요.
- **목록에 없는 30fps로 시작되지 않음.** 드라이버가 30fps를 거부했거나 다른
  속도로 적용한 경우입니다. 진단 로그의 `Direct 30 fps request` 줄을 확인하고,
  목록에 있는 속도를 고르거나 **목록에 없는 FPS 요청**을 끄세요.
- **단축키가 동작하지 않음.** 영상 창을 클릭해 LLCV에 포커스를 준 뒤
  **설정 → 안내 · 로그**에서 키를 확인하세요. 영상 전용 동작은 오디오 전용
  모드에서 동작하지 않습니다. 설정 키를 잊었다면 메뉴 막대(**Alt**)에서 설정을
  열거나 `llcv --settings`로 실행한 뒤 **기본값 복원**을 누르세요.
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
만들었습니다. Dear ImGui는 MIT, Pretendard와 Noto Sans JP 한자 서브셋은
SIL Open Font License 1.1을 따릅니다.

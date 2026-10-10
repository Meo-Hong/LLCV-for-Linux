# LLCV 리눅스판 빌드 안내

> [English](BUILDING.md) · [LLCV 리눅스판 소개](README.ko.md)

이 문서는 소스 코드로 LLCV 설치 패키지를 만들고 설치하는 방법을 배포판별로
정리한 것입니다. 처음이라면 [2. 가장 간단한 방법](#2-가장-간단한-방법)만
따라 하면 됩니다.

## 목차

1. [내 배포판에 맞는 방법 찾기](#1-내-배포판에-맞는-방법-찾기)
2. [가장 간단한 방법](#2-가장-간단한-방법)
3. [배포판별 수동 빌드](#3-배포판별-수동-빌드)
4. [Docker로 다른 배포판·ARM용 빌드](#4-docker로-다른-배포판arm용-빌드)
5. [설치, 확인, 제거](#5-설치-확인-제거)
6. [패키지 없이 직접 빌드 (그 밖의 배포판)](#6-패키지-없이-직접-빌드-그-밖의-배포판)
7. [빌드 문제 해결](#7-빌드-문제-해결)

## 1. 내 배포판에 맞는 방법 찾기

먼저 배포판을 확인합니다.

```sh
cat /etc/os-release
```

`ID`와 `VERSION_ID`를 보고 아래 표에서 찾으세요.

| 배포판 | 패키지 형식 | SDL3 | 수동 빌드 |
| --- | --- | --- | --- |
| Ubuntu 25.10, 26.04 | `.deb` | 시스템 패키지 | [3.1](#31-ubuntu--debian-계열-deb) |
| Ubuntu 24.04, Linux Mint 22, Pop!_OS 24.04, Zorin OS 18 | `.deb` | 앱에 포함 (자동으로 받음) | [3.1](#31-ubuntu--debian-계열-deb) |
| Ubuntu 22.04, Linux Mint 21, Pop!_OS 22.04, Zorin OS 17, elementary OS 7 | `.deb` | 앱에 포함 (자동으로 받음) | [3.1](#31-ubuntu--debian-계열-deb) |
| Debian 13, Kali, Raspberry Pi OS (trixie) | `.deb` | 시스템 패키지 | [3.1](#31-ubuntu--debian-계열-deb) |
| Debian 12, LMDE 6, Raspberry Pi OS (bookworm) | `.deb` | 앱에 포함 (자동으로 받음) | [3.1](#31-ubuntu--debian-계열-deb) |
| Fedora 43 이상, Nobara | `.rpm` | 시스템 패키지 | [3.2](#32-fedora--rhel-계열-rpm) |
| RHEL, Rocky Linux, AlmaLinux, CentOS Stream 9·10 | `.rpm` | 앱에 포함 (자동으로 받음) | [3.2](#32-fedora--rhel-계열-rpm) |
| Arch Linux, Manjaro, EndeavourOS | `.pkg.tar.zst` | 시스템 패키지 | [3.3](#33-arch-계열-pkgtarzst) |
| 그 밖 (openSUSE, Gentoo, Void 등) | 패키지 없음 | 시스템 또는 포함 | [6](#6-패키지-없이-직접-빌드-그-밖의-배포판) |

"앱에 포함"은 그 배포판 저장소에 SDL3가 없다는 뜻입니다. 이 경우 빌드할 때
고정된 버전(SDL3 3.4.18, SHA256 검증)을 인터넷에서 받아 앱 안에 넣습니다.

**알아둘 점**

- `.deb`는 빌드한 배포판 버전(또는 그보다 새 버전)에만 설치됩니다. 예를 들어
  Ubuntu 26.04에서 만든 패키지는 Ubuntu 22.04나 24.04에 설치되지 않습니다.
  더 새로운 C/C++ 라이브러리와 시스템 SDL3 패키지가 필요하기 때문입니다. 반대로
  Ubuntu 22.04에서 만든 패키지는 Ubuntu 24.04, 26.04와 Debian 12, 13에도
  설치되고, AlmaLinux 9에서 만든 `.rpm`은 EL 10과 Fedora에도 설치됩니다.
  배포용 패키지는 이 점을 이용합니다
  ([packaging/README.ko.md](packaging/README.ko.md#배포용-패키지)). 다른
  배포판용은 그 배포판에서 직접 빌드하거나
  [4장의 Docker 빌드](#4-docker로-다른-배포판arm용-빌드)를 쓰세요.
- Raspberry Pi OS 64비트는 `arm64`, 32비트는 `armhf`를 씁니다. 32비트는
  Raspberry Pi 2 이상에서 동작합니다(Pi Zero, Pi 1 미지원).

## 2. 가장 간단한 방법

지원하는 모든 배포판에서 같은 명령으로 빌드할 수 있습니다.

**① git 설치**

```sh
sudo apt install git        # Ubuntu, Debian, Raspberry Pi OS, Mint
sudo dnf install git        # Fedora, RHEL, Rocky, AlmaLinux
sudo pacman -S git          # Arch, Manjaro
```

**② 소스 받기**

```sh
git clone https://github.com/Meo-Hong/LLCV-for-Linux.git
cd LLCV-for-Linux/linux
```

**③ 빌드 도구와 라이브러리 설치**

```sh
./tools/install-build-deps.sh
```

배포판을 자동으로 알아내서 필요한 패키지를 설치합니다. 관리자 비밀번호를
물을 수 있습니다.

**④ 패키지 만들기**

```sh
./tools/build-package.sh
```

마지막에 만들어진 파일과 설치 명령이 표시됩니다. 예시:

```text
Package: /home/사용자/LLCV-for-Linux/linux/dist/ubuntu-26.04/llcv_2.0.2_amd64.deb
Install: sudo apt install /home/사용자/LLCV-for-Linux/linux/dist/ubuntu-26.04/llcv_2.0.2_amd64.deb
```

**⑤ 설치**

화면에 표시된 `Install:` 명령을 그대로 실행합니다. 설치가 끝나면 앱 메뉴에서
**LLCV**를 실행하거나 터미널에서 `llcv`를 입력합니다.

> 소스를 받은 뒤 업데이트하려면 `git pull` 후 ④, ⑤를 다시 하면 됩니다.

## 3. 배포판별 수동 빌드

스크립트가 하는 일을 직접 하고 싶거나, 스크립트가 실패했을 때 쓰는 방법입니다.
모든 명령은 `LLCV-for-Linux/linux` 폴더에서 실행합니다.

### 3.1 Ubuntu / Debian 계열 (.deb)

Ubuntu, Debian, Raspberry Pi OS, Linux Mint, Pop!_OS, Zorin OS, LMDE, Kali에
해당합니다.

**기본 도구 설치**

```sh
sudo apt update
sudo apt install build-essential debhelper dpkg-dev pkgconf ca-certificates curl
```

**빌드 의존성 설치** — 배포판 저장소에 SDL3가 있는지에 따라 다릅니다.

```sh
apt-cache show libsdl3-dev >/dev/null 2>&1 && echo "SDL3 있음" || echo "SDL3 없음"
```

```sh
sudo apt build-dep ./
```

위 명령은 SDL3가 있을 때(Ubuntu 25.10 이상, Debian 13 이상) 씁니다. SDL3가
없을 때(Ubuntu 22.04, Ubuntu 24.04, Debian 12 계열)는 아래 명령을 씁니다.

```sh
sudo apt build-dep -P pkg.llcv.bundled-sdl3 ./
```

`-P pkg.llcv.bundled-sdl3`는 SDL3를 앱에 넣어 빌드할 때 필요한 X11, Wayland,
오디오 개발 패키지를 함께 설치하라는 뜻입니다.

> Ubuntu에서 `libsdl3-dev`나 `libdecor-0-dev`가 보이지 않으면 universe
> 저장소가 꺼져 있을 수 있습니다. `sudo add-apt-repository universe` 후 다시
> 시도하세요.

**패키지 만들기**

```sh
./tools/build-deb.sh
```

SDL3가 없으면 스크립트가 알아서 SDL3 소스를 받고(`tools/fetch-sdl3.sh`) 앱에
넣어 빌드합니다.

**결과물**: `dist/<배포판>-<버전>/llcv_<버전>~<배포판><버전>_<아키텍처>.deb`
(예: `dist/debian-13/llcv_2.0.2_arm64.deb`)

**설치** — 폴더 이름은 내 배포판(`ID`-`VERSION_ID`)입니다.

```sh
sudo apt install ./dist/$(. /etc/os-release; echo "$ID-$VERSION_ID")/llcv_*_$(dpkg --print-architecture).deb
```

**Raspberry Pi에서 직접 빌드할 때**

Pi에서도 위 순서 그대로 빌드됩니다. 다만 시간이 오래 걸리고 메모리가 부족하면
빌드가 멈출 수 있습니다. Pi 4 (2GB 이하)라면 병렬 작업 수를 줄이세요.

```sh
DEB_BUILD_OPTIONS=parallel=2 ./tools/build-deb.sh
```

PC에서 Docker로 Pi용 패키지를 만드는 편이 훨씬 빠릅니다([4장](#4-docker로-다른-배포판arm용-빌드)).

### 3.2 Fedora / RHEL 계열 (.rpm)

Fedora, RHEL, Rocky Linux, AlmaLinux, CentOS Stream에 해당합니다.

**기본 도구 설치**

```sh
sudo dnf install rpm-build tar gzip pkgconf
```

`curl`이 없다면 함께 설치하세요(`sudo dnf install curl`). RHEL 계열 기본 설치에
들어 있는 `curl-minimal`로도 충분하니, 이미 있다면 따로 설치하지 마세요. 둘을
같이 설치하려 하면 충돌 오류가 납니다.

RHEL 계열(Fedora 제외)은 개발 패키지가 있는 CRB 저장소를 켜야 합니다.

```sh
sudo dnf install dnf-plugins-core
sudo dnf config-manager --set-enabled crb
```

위는 Rocky Linux, AlmaLinux, CentOS Stream용입니다. RHEL은 구독 저장소를
켭니다.

```sh
sudo subscription-manager repos --enable codeready-builder-for-rhel-$(rpm -E %rhel)-$(uname -m)-rpms
```

**빌드 의존성 설치**

```sh
sudo dnf builddep packaging/rpm/llcv.spec
```

Fedora는 시스템 SDL3를 쓰고, RHEL 계열은 SDL3를 앱에 넣는 데 필요한 패키지가
자동으로 선택됩니다.

**패키지 만들기**

```sh
./tools/build-rpm.sh
```

**결과물**: `dist/<배포판>-<버전>/llcv-2.0.2-1.x86_64.rpm`
(디버그 정보 `llcv-debuginfo`, `llcv-debugsource`와 소스 RPM도 함께 생깁니다.)

**설치** — 폴더 이름은 내 배포판(`ID`-`VERSION_ID`)입니다.

```sh
sudo dnf install ./dist/$(. /etc/os-release; echo "$ID-$VERSION_ID")/llcv-2*.$(uname -m).rpm
```

### 3.3 Arch 계열 (.pkg.tar.zst)

Arch Linux, Manjaro, EndeavourOS에 해당합니다.

**빌드 의존성 설치**

```sh
sudo pacman -S --needed base-devel cmake pkgconf sdl3 libjpeg-turbo libpng wayland wayland-protocols libglvnd
```

**패키지 만들기** — `makepkg`는 일반 사용자로 실행해야 합니다(root 불가).

```sh
cd packaging/arch
makepkg -f
```

**결과물**: `packaging/arch/llcv-2.0.2-1-x86_64.pkg.tar.zst`
(`./tools/build-package.sh`를 쓰면 `dist/arch/`로 복사됩니다.)

**설치**

```sh
sudo pacman -U llcv-2.0.2-1-*.pkg.tar.zst
```

`makepkg -si`를 쓰면 의존성 설치, 빌드, 설치를 한 번에 합니다.

## 4. Docker로 다른 배포판·ARM용 빌드

내 PC와 다른 배포판이나 다른 CPU(라즈베리파이 등)용 패키지가 필요하면, Docker
컨테이너 안에서 그 배포판을 그대로 써서 빌드합니다. 내 PC에는 Docker만 있으면
되고, 결과물은 `dist/` 아래에 생깁니다.

**준비**

- Docker 설치, 그리고 사용자를 `docker` 그룹에 추가(`sudo usermod -aG docker $USER`
  후 다시 로그인)
- 인터넷 연결 (컨테이너 이미지와 빌드 의존성을 받습니다)

**명령과 결과물**

| 명령 | 대상 | 결과물 위치 |
| --- | --- | --- |
| `./tools/build-deb-docker.sh ubuntu:22.04` | Ubuntu 22.04, Mint 21, Pop!_OS 22.04 | `dist/ubuntu-22.04/` |
| `./tools/build-deb-docker.sh ubuntu:24.04` | Ubuntu 24.04, Mint 22, Pop!_OS 24.04 | `dist/ubuntu-24.04/` |
| `./tools/build-deb-docker.sh ubuntu:26.04` | Ubuntu 26.04 | `dist/ubuntu-26.04/` |
| `./tools/build-deb-docker.sh debian:12` | Debian 12, LMDE 6 | `dist/debian-12/` |
| `./tools/build-deb-docker.sh debian:13` | Debian 13, Kali | `dist/debian-13/` |
| `./tools/build-deb-docker.sh debian:13 arm64` | Raspberry Pi OS 64비트 (trixie) | `dist/debian-13/` |
| `./tools/build-deb-docker.sh debian:13 armhf` | Raspberry Pi OS 32비트 (trixie) | `dist/debian-13/` |
| `./tools/build-deb-docker.sh debian:12 arm64` | Raspberry Pi OS 64비트 (bookworm) | `dist/debian-12/` |
| `./tools/build-rpm-docker.sh fedora:latest` | Fedora | `dist/fedora-<버전>/` |
| `./tools/build-rpm-docker.sh almalinux:10` | RHEL, Rocky, AlmaLinux 10 | `dist/almalinux-<버전>/` |
| `./tools/build-rpm-docker.sh almalinux:9` | RHEL, Rocky, AlmaLinux 9 | `dist/almalinux-<버전>/` |
| `./tools/build-arch-docker.sh` | Arch, Manjaro, EndeavourOS | `dist/arch/` |

- ARM용(`arm64`, `armhf`)은 에뮬레이션 없이 교차 컴파일하므로 PC 속도로
  빌드됩니다.
- 내려받은 패키지는 Docker 볼륨(`llcv-apt-cache-*`, `llcv-dnf-cache-*`,
  `llcv-pacman-cache-*`)에 저장되어, 두 번째 빌드부터 빨라집니다.
- Docker Desktop이 꺼져 있으면 스크립트가 시스템 Docker(`default` 컨텍스트)를
  자동으로 씁니다.

## 5. 설치, 확인, 제거

| | Ubuntu / Debian 계열 | Fedora / RHEL 계열 | Arch 계열 |
| --- | --- | --- | --- |
| 설치 | `sudo apt install ./파일.deb` | `sudo dnf install ./파일.rpm` | `sudo pacman -U 파일.pkg.tar.zst` |
| 설치 확인 | `dpkg -l llcv` | `rpm -q llcv` | `pacman -Q llcv` |
| 설치된 파일 | `dpkg -L llcv` | `rpm -ql llcv` | `pacman -Ql llcv` |
| 제거 | `sudo apt remove llcv` | `sudo dnf remove llcv` | `sudo pacman -R llcv` |

`.deb`를 `sudo dpkg -i`로 설치해도 되지만, 그러면 부족한 의존성을 자동으로
설치하지 않습니다. 이때는 `sudo apt -f install`로 마무리하세요.

설치 후 확인:

```sh
llcv --version
```

설치되는 위치:

| 항목 | 위치 |
| --- | --- |
| 실행 파일 | `/usr/bin/llcv` |
| 앱 메뉴 항목 | `/usr/share/applications/io.github.seria_aa.LLCV.desktop` |
| 아이콘 | `/usr/share/icons/hicolor/*/apps/io.github.seria_aa.LLCV.png` |
| 글꼴 | `/usr/share/llcv/fonts/` |
| 설명서 | `man llcv` |

사용자별 데이터는 제거해도 남습니다. 완전히 지우려면 아래 폴더를 지우세요.

| 항목 | 위치 |
| --- | --- |
| 설정 | `~/.config/llcv/` |
| 로그 | `~/.local/state/llcv/` |
| 스크린샷 | `~/사진/LLCV/` (사진 폴더 이름은 시스템 언어를 따름) |

## 6. 패키지 없이 직접 빌드 (그 밖의 배포판)

패키지 형식을 지원하지 않는 배포판이거나, 설치하지 않고 실행만 해 보고 싶을 때
씁니다.

**필요한 것** (pkg-config 이름 기준. 배포판마다 패키지 이름은 다릅니다)

| 용도 | pkg-config 이름 | 필수 |
| --- | --- | --- |
| C/C++ 컴파일러 (C++20), CMake 3.22 이상 | - | 예 |
| 창, 입력, 오디오 | `sdl3` (3.2 이상) | 예 (없으면 아래 "SDL3가 없을 때") |
| MJPEG 디코딩 | `libturbojpeg` | 예 |
| 스크린샷 PNG | `libpng` | 예 |
| HDR 출력 (Wayland) | `wayland-client`, `wayland-scanner` | 아니요 |

openSUSE는 pkg-config 이름으로 바로 설치할 수 있습니다(검증하지 않은 예시).

```sh
sudo zypper install cmake gcc-c++ 'pkgconfig(sdl3)' 'pkgconfig(libturbojpeg)' 'pkgconfig(libpng)' 'pkgconfig(wayland-client)' 'pkgconfig(wayland-scanner)'
```

**빌드와 실행**

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/llcv
```

설치하지 않고 실행해도 글꼴과 아이콘은 소스 폴더에서 찾습니다.

**시스템에 설치 / 제거**

```sh
sudo cmake --install build
sudo xargs rm -v < build/install_manifest.txt
```

위는 설치, 아래는 제거입니다. 기본 설치 위치는 `/usr/local`입니다.

**SDL3가 없을 때**

```sh
./tools/fetch-sdl3.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLLCV_SDL3=BUNDLED
cmake --build build -j
```

이때는 SDL3를 빌드하기 위한 X11, Wayland, EGL/OpenGL, PipeWire, PulseAudio,
ALSA, D-Bus 개발 패키지가 필요합니다.

**CMake 옵션**

| 옵션 | 기본값 | 의미 |
| --- | --- | --- |
| `LLCV_SDL3` | `AUTO` | `SYSTEM`(시스템 SDL3), `BUNDLED`(받아 둔 SDL3를 정적으로 포함), `AUTO`(시스템 우선) |
| `LLCV_WAYLAND_COLOR_MANAGEMENT` | `AUTO` | Wayland HDR 출력 포함 여부. `ON`이면 필요한 패키지가 없을 때 오류 |
| `LLCV_USE_SYSTEM_IMGUI` | `OFF` | 함께 들어 있는 Dear ImGui 대신 시스템 Dear ImGui 1.92 이상 사용 |
| `LLCV_SOURCE_TREE_DATA` | `ON` | 설치 없이 실행할 때 소스 폴더에서 글꼴·아이콘 찾기 (패키지 빌드에서는 끔) |

## 7. 빌드 문제 해결

| 메시지 | 원인과 해결 |
| --- | --- |
| `Unsupported distribution` | 스크립트가 모르는 배포판입니다. [6장](#6-패키지-없이-직접-빌드-그-밖의-배포판)의 직접 빌드를 쓰세요. |
| `dpkg-checkbuilddeps: error: Unmet build dependencies` | 빌드 의존성이 빠졌습니다. `./tools/install-build-deps.sh`를 먼저 실행하세요. |
| `... the package libsdl3-dev cannot be found` / `Unable to locate package libsdl3-dev` | 그 배포판에 SDL3가 없습니다. `sudo apt build-dep -P pkg.llcv.bundled-sdl3 ./`를 쓰세요. Ubuntu라면 universe 저장소도 확인하세요. |
| `No match for argument` / `No matching package to install` (dnf) | RHEL 계열에서 CRB 저장소가 꺼져 있습니다. [3.2](#32-fedora--rhel-계열-rpm)를 보세요. |
| `SDL3 3.2 or newer is not installed. Run tools/fetch-sdl3.sh` | 직접 빌드에서 SDL3가 없습니다. `./tools/fetch-sdl3.sh` 후 `-DLLCV_SDL3=BUNDLED`로 다시 설정하세요. |
| `SHA256 mismatch` | 받다가 깨진 SDL3 파일입니다. `third_party/sdl3/`를 지우고 다시 실행하세요. |
| `curl-minimal ... conflicts with curl` (dnf) | RHEL 계열에 이미 있는 `curl-minimal`과 충돌합니다. `curl`은 설치 목록에서 빼세요. |
| `curl: (6) Could not resolve host` | 인터넷 연결 문제입니다. SDL3를 받는 배포판은 인터넷이 필요합니다. |
| `ERROR: Running makepkg as root is not allowed` | Arch에서는 일반 사용자로 빌드하세요. |
| `permission denied ... docker.sock` | `sudo usermod -aG docker $USER` 후 다시 로그인하세요. |
| `Wayland HDR output (wp_color_management_v1): OFF` | HDR 출력 없이 빌드됩니다. `wayland-scanner`(Debian 계열은 `libwayland-bin`)와 `wayland-client` 개발 패키지를 설치하세요. |
| `CMake 3.22 or higher is required` | 배포판이 너무 오래됐습니다(예: Ubuntu 20.04, Debian 11). Ubuntu 22.04, Debian 12가 가장 오래된 지원 릴리스입니다. |
| `.deb` 설치 시 `libc6 (>= 2.xx)`, `libstdc++6 (>= 13)`, `libsdl3-0` 의존성 오류 | 더 새 배포판에서 만든 패키지입니다. 설치할 배포판에서 다시 빌드하거나 [4장](#4-docker로-다른-배포판arm용-빌드)을 쓰세요. |
| 빌드 중 멈춤, `Killed`, `internal compiler error` | 메모리 부족입니다(주로 Raspberry Pi). `DEB_BUILD_OPTIONS=parallel=2` 또는 `cmake --build build -j2`로 병렬 수를 줄이세요. |

빌드에 성공했는데 앱이 이상하게 동작하면 [README의 문제 해결](README.ko.md#문제-해결)을
보세요.

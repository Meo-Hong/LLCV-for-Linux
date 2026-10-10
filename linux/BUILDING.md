# Building LLCV for Linux

> [한국어](BUILDING.ko.md) · [About LLCV for Linux](README.md)

This guide explains how to build an LLCV install package from source and
install it, distribution by distribution. If this is your first time, follow
[2. Quick start](#2-quick-start) only.

## Contents

1. [Find the method for your distribution](#1-find-the-method-for-your-distribution)
2. [Quick start](#2-quick-start)
3. [Manual builds by distribution](#3-manual-builds-by-distribution)
4. [Building for other distributions and ARM with Docker](#4-building-for-other-distributions-and-arm-with-docker)
5. [Install, check, remove](#5-install-check-remove)
6. [Building without a package (other distributions)](#6-building-without-a-package-other-distributions)
7. [Build troubleshooting](#7-build-troubleshooting)

## 1. Find the method for your distribution

First, check your distribution:

```sh
cat /etc/os-release
```

Find your `ID` and `VERSION_ID` in this table:

| Distribution | Package | SDL3 | Manual build |
| --- | --- | --- | --- |
| Ubuntu 25.10, 26.04 | `.deb` | system package | [3.1](#31-ubuntu-and-debian-family-deb) |
| Ubuntu 24.04, Linux Mint 22, Pop!_OS 24.04, Zorin OS 18 | `.deb` | built in (downloaded automatically) | [3.1](#31-ubuntu-and-debian-family-deb) |
| Ubuntu 22.04, Linux Mint 21, Pop!_OS 22.04, Zorin OS 17, elementary OS 7 | `.deb` | built in (downloaded automatically) | [3.1](#31-ubuntu-and-debian-family-deb) |
| Debian 13, Kali, Raspberry Pi OS (trixie) | `.deb` | system package | [3.1](#31-ubuntu-and-debian-family-deb) |
| Debian 12, LMDE 6, Raspberry Pi OS (bookworm) | `.deb` | built in (downloaded automatically) | [3.1](#31-ubuntu-and-debian-family-deb) |
| Fedora 43 or newer, Nobara | `.rpm` | system package | [3.2](#32-fedora-and-rhel-family-rpm) |
| RHEL, Rocky Linux, AlmaLinux, CentOS Stream 9 and 10 | `.rpm` | built in (downloaded automatically) | [3.2](#32-fedora-and-rhel-family-rpm) |
| Arch Linux, Manjaro, EndeavourOS | `.pkg.tar.zst` | system package | [3.3](#33-arch-family-pkgtarzst) |
| Others (openSUSE, Gentoo, Void, ...) | no package | system or built in | [6](#6-building-without-a-package-other-distributions) |

"Built in" means the distribution has no SDL3 package. The build then
downloads a pinned version (SDL3 3.4.18, SHA256 checked) and links it into
the program.

**Good to know**

- A `.deb` installs only on the release it was built on, or a newer one. For
  example, a package built on Ubuntu 26.04 does not install on Ubuntu 22.04 or
  24.04: it needs newer C and C++ libraries, and the system SDL3 package.
  The other way works: a package built on Ubuntu 22.04 also installs on
  Ubuntu 24.04 and 26.04 and on Debian 12 and 13, and an `.rpm` built on
  AlmaLinux 9 also installs on EL 10 and Fedora. The release packages use this
  (see [packaging/README.md](packaging/README.md#release-packages)).
  For another release, build on that release, or use the
  [Docker builds in section 4](#4-building-for-other-distributions-and-arm-with-docker).
- Raspberry Pi OS 64-bit uses `arm64`; 32-bit uses `armhf`. The 32-bit
  package needs a Raspberry Pi 2 or newer (Pi Zero and Pi 1 are not supported).

## 2. Quick start

The same commands work on every supported distribution.

**1. Install git**

```sh
sudo apt install git        # Ubuntu, Debian, Raspberry Pi OS, Mint
sudo dnf install git        # Fedora, RHEL, Rocky, AlmaLinux
sudo pacman -S git          # Arch, Manjaro
```

**2. Get the source**

```sh
git clone https://github.com/Meo-Hong/LLCV-for-Linux.git
cd LLCV-for-Linux/linux
```

**3. Install the build tools and libraries**

```sh
./tools/install-build-deps.sh
```

The script detects your distribution and installs what is needed. It may ask
for your password.

**4. Build the package**

```sh
./tools/build-package.sh
```

At the end, it prints the package and the install command, for example:

```text
Package: /home/user/LLCV-for-Linux/linux/dist/ubuntu-26.04/llcv_2.0.2_amd64.deb
Install: sudo apt install /home/user/LLCV-for-Linux/linux/dist/ubuntu-26.04/llcv_2.0.2_amd64.deb
```

**5. Install**

Run the printed `Install:` command. Then start **LLCV** from the application
menu, or type `llcv` in a terminal.

> To update later, run `git pull`, then steps 4 and 5 again.

## 3. Manual builds by distribution

Use these steps to do what the scripts do by hand, or when a script fails. Run
every command in the `LLCV-for-Linux/linux` folder.

### 3.1 Ubuntu and Debian family (.deb)

For Ubuntu, Debian, Raspberry Pi OS, Linux Mint, Pop!_OS, Zorin OS, LMDE and
Kali.

**Install the base tools**

```sh
sudo apt update
sudo apt install build-essential debhelper dpkg-dev pkgconf ca-certificates curl
```

**Install the build dependencies.** This depends on whether your release has
SDL3:

```sh
apt-cache show libsdl3-dev >/dev/null 2>&1 && echo "SDL3 available" || echo "no SDL3"
```

With SDL3 (Ubuntu 25.10 or newer, Debian 13 or newer):

```sh
sudo apt build-dep ./
```

Without SDL3 (the Ubuntu 22.04, Ubuntu 24.04 and Debian 12 families):

```sh
sudo apt build-dep -P pkg.llcv.bundled-sdl3 ./
```

`-P pkg.llcv.bundled-sdl3` also installs the X11, Wayland and audio
development packages that are needed to build SDL3 into the program.

> If Ubuntu cannot find `libsdl3-dev` or `libdecor-0-dev`, the universe
> repository may be off.
> Run `sudo add-apt-repository universe` and try again.

**Build the package**

```sh
./tools/build-deb.sh
```

Without SDL3, the script downloads the SDL3 source (`tools/fetch-sdl3.sh`)
and builds it in.

**Result**: `dist/<distro>-<version>/llcv_<version>~<distro><version>_<arch>.deb`
(for example `dist/debian-13/llcv_2.0.2_arm64.deb`)

**Install.** The folder name is your `ID`-`VERSION_ID`:

```sh
sudo apt install ./dist/$(. /etc/os-release; echo "$ID-$VERSION_ID")/llcv_*_$(dpkg --print-architecture).deb
```

**Building on a Raspberry Pi**

The same steps work on the Pi itself. The build takes a while, and it can
stop when memory runs out. On a Pi with 2 GB or less, reduce the number of
parallel jobs:

```sh
DEB_BUILD_OPTIONS=parallel=2 ./tools/build-deb.sh
```

Building the Pi package on a PC with Docker is much faster
([section 4](#4-building-for-other-distributions-and-arm-with-docker)).

### 3.2 Fedora and RHEL family (.rpm)

For Fedora, RHEL, Rocky Linux, AlmaLinux and CentOS Stream.

**Install the base tools**

```sh
sudo dnf install rpm-build tar gzip pkgconf
```

Install `curl` too if it is missing (`sudo dnf install curl`). The
`curl-minimal` that the RHEL family installs by default is enough; asking for
`curl` on top of it fails with a conflict.

The RHEL family (not Fedora) also needs the CRB repository, which holds the
development packages. On Rocky Linux, AlmaLinux and CentOS Stream:

```sh
sudo dnf install dnf-plugins-core
sudo dnf config-manager --set-enabled crb
```

On RHEL, enable the subscription repository instead:

```sh
sudo subscription-manager repos --enable codeready-builder-for-rhel-$(rpm -E %rhel)-$(uname -m)-rpms
```

**Install the build dependencies**

```sh
sudo dnf builddep packaging/rpm/llcv.spec
```

Fedora uses the system SDL3. On the RHEL family, the packages needed to build
SDL3 into the program are selected automatically.

**Build the package**

```sh
./tools/build-rpm.sh
```

**Result**: `dist/<distro>-<version>/llcv-2.0.2-1.x86_64.rpm`
(debug packages `llcv-debuginfo` and `llcv-debugsource`, and a source RPM, are
created too)

**Install.** The folder name is your `ID`-`VERSION_ID`:

```sh
sudo dnf install ./dist/$(. /etc/os-release; echo "$ID-$VERSION_ID")/llcv-2*.$(uname -m).rpm
```

### 3.3 Arch family (.pkg.tar.zst)

For Arch Linux, Manjaro and EndeavourOS.

**Install the build dependencies**

```sh
sudo pacman -S --needed base-devel cmake pkgconf sdl3 libjpeg-turbo libpng wayland wayland-protocols libglvnd
```

**Build the package.** Run `makepkg` as a normal user (not root):

```sh
cd packaging/arch
makepkg -f
```

**Result**: `packaging/arch/llcv-2.0.2-1-x86_64.pkg.tar.zst`
(`./tools/build-package.sh` also copies it to `dist/arch/`)

**Install**

```sh
sudo pacman -U llcv-2.0.2-1-*.pkg.tar.zst
```

`makepkg -si` installs the dependencies, builds and installs in one step.

## 4. Building for other distributions and ARM with Docker

To build a package for another distribution, or for another CPU such as a
Raspberry Pi, the scripts run that distribution inside a Docker container. Your
PC only needs Docker. The packages are written under `dist/`.

**Requirements**

- Docker, with your user in the `docker` group (`sudo usermod -aG docker $USER`,
  then log in again)
- An internet connection (for the container images and build dependencies)

**Commands and results**

| Command | For | Result folder |
| --- | --- | --- |
| `./tools/build-deb-docker.sh ubuntu:22.04` | Ubuntu 22.04, Mint 21, Pop!_OS 22.04 | `dist/ubuntu-22.04/` |
| `./tools/build-deb-docker.sh ubuntu:24.04` | Ubuntu 24.04, Mint 22, Pop!_OS 24.04 | `dist/ubuntu-24.04/` |
| `./tools/build-deb-docker.sh ubuntu:26.04` | Ubuntu 26.04 | `dist/ubuntu-26.04/` |
| `./tools/build-deb-docker.sh debian:12` | Debian 12, LMDE 6 | `dist/debian-12/` |
| `./tools/build-deb-docker.sh debian:13` | Debian 13, Kali | `dist/debian-13/` |
| `./tools/build-deb-docker.sh debian:13 arm64` | Raspberry Pi OS 64-bit (trixie) | `dist/debian-13/` |
| `./tools/build-deb-docker.sh debian:13 armhf` | Raspberry Pi OS 32-bit (trixie) | `dist/debian-13/` |
| `./tools/build-deb-docker.sh debian:12 arm64` | Raspberry Pi OS 64-bit (bookworm) | `dist/debian-12/` |
| `./tools/build-rpm-docker.sh fedora:latest` | Fedora | `dist/fedora-<version>/` |
| `./tools/build-rpm-docker.sh almalinux:10` | RHEL, Rocky, AlmaLinux 10 | `dist/almalinux-<version>/` |
| `./tools/build-rpm-docker.sh almalinux:9` | RHEL, Rocky, AlmaLinux 9 | `dist/almalinux-<version>/` |
| `./tools/build-arch-docker.sh` | Arch, Manjaro, EndeavourOS | `dist/arch/` |

- ARM packages (`arm64`, `armhf`) are cross-compiled without emulation, so
  they build at PC speed.
- Downloaded packages are kept in Docker volumes (`llcv-apt-cache-*`,
  `llcv-dnf-cache-*`, `llcv-pacman-cache-*`), so later builds are faster.
- When Docker Desktop is not running, the scripts use the system Docker
  (`default` context) automatically.

## 5. Install, check, remove

| | Ubuntu / Debian family | Fedora / RHEL family | Arch family |
| --- | --- | --- | --- |
| Install | `sudo apt install ./file.deb` | `sudo dnf install ./file.rpm` | `sudo pacman -U file.pkg.tar.zst` |
| Check | `dpkg -l llcv` | `rpm -q llcv` | `pacman -Q llcv` |
| Installed files | `dpkg -L llcv` | `rpm -ql llcv` | `pacman -Ql llcv` |
| Remove | `sudo apt remove llcv` | `sudo dnf remove llcv` | `sudo pacman -R llcv` |

`sudo dpkg -i` also installs a `.deb`, but it does not install missing
dependencies. Finish with `sudo apt -f install` in that case.

After installing:

```sh
llcv --version
```

Installed locations:

| Item | Location |
| --- | --- |
| Program | `/usr/bin/llcv` |
| Menu entry | `/usr/share/applications/io.github.seria_aa.LLCV.desktop` |
| Icons | `/usr/share/icons/hicolor/*/apps/io.github.seria_aa.LLCV.png` |
| Fonts | `/usr/share/llcv/fonts/` |
| Manual | `man llcv` |

Per-user data stays after removal. To remove it too, delete these folders:

| Item | Location |
| --- | --- |
| Settings | `~/.config/llcv/` |
| Logs | `~/.local/state/llcv/` |
| Screenshots | `~/Pictures/LLCV/` (the folder name follows the system language) |

## 6. Building without a package (other distributions)

Use this on distributions without a supported package format, or to run the
app without installing it.

**Requirements** (by pkg-config name; package names differ by distribution)

| Purpose | pkg-config name | Required |
| --- | --- | --- |
| C/C++ compiler (C++20), CMake 3.22 or newer | - | yes |
| Windows, input, audio | `sdl3` (3.2 or newer) | yes (see "Without SDL3" below) |
| MJPEG decoding | `libturbojpeg` | yes |
| PNG screenshots | `libpng` | yes |
| HDR output (Wayland) | `wayland-client`, `wayland-scanner` | no |

openSUSE can install by pkg-config name (an untested example):

```sh
sudo zypper install cmake gcc-c++ 'pkgconfig(sdl3)' 'pkgconfig(libturbojpeg)' 'pkgconfig(libpng)' 'pkgconfig(wayland-client)' 'pkgconfig(wayland-scanner)'
```

**Build and run**

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/llcv
```

Run from the build folder, the app finds its fonts and icons in the source tree.

**Install into the system, and remove**

```sh
sudo cmake --install build
sudo xargs rm -v < build/install_manifest.txt
```

The first command installs, the second removes. The default prefix is
`/usr/local`.

**Without SDL3**

```sh
./tools/fetch-sdl3.sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DLLCV_SDL3=BUNDLED
cmake --build build -j
```

Building SDL3 in needs the X11, Wayland, EGL/OpenGL, PipeWire, PulseAudio,
ALSA and D-Bus development packages.

**CMake options**

| Option | Default | Meaning |
| --- | --- | --- |
| `LLCV_SDL3` | `AUTO` | `SYSTEM` (system SDL3), `BUNDLED` (link the downloaded SDL3 statically), `AUTO` (system first) |
| `LLCV_WAYLAND_COLOR_MANAGEMENT` | `AUTO` | Build HDR output for Wayland; `ON` makes missing dependencies an error |
| `LLCV_USE_SYSTEM_IMGUI` | `OFF` | Use a system Dear ImGui 1.92 or newer instead of the bundled copy |
| `LLCV_SOURCE_TREE_DATA` | `ON` | Find fonts and icons in the source tree when not installed (off for packages) |

## 7. Build troubleshooting

| Message | Cause and fix |
| --- | --- |
| `Unsupported distribution` | The scripts do not know this distribution. Use the [build without a package](#6-building-without-a-package-other-distributions). |
| `dpkg-checkbuilddeps: error: Unmet build dependencies` | Build dependencies are missing. Run `./tools/install-build-deps.sh` first. |
| `... the package libsdl3-dev cannot be found` / `Unable to locate package libsdl3-dev` | The release has no SDL3. Use `sudo apt build-dep -P pkg.llcv.bundled-sdl3 ./`. On Ubuntu, also check the universe repository. |
| `No match for argument` / `No matching package to install` (dnf) | The CRB repository is off on the RHEL family. See [3.2](#32-fedora-and-rhel-family-rpm). |
| `SDL3 3.2 or newer is not installed. Run tools/fetch-sdl3.sh` | A direct build without SDL3. Run `./tools/fetch-sdl3.sh`, then configure with `-DLLCV_SDL3=BUNDLED`. |
| `SHA256 mismatch` | The SDL3 download is damaged. Delete `third_party/sdl3/` and run again. |
| `curl-minimal ... conflicts with curl` (dnf) | The RHEL family already has `curl-minimal`. Leave `curl` out of the install list. |
| `curl: (6) Could not resolve host` | No internet connection. Releases that build SDL3 in need to download it. |
| `ERROR: Running makepkg as root is not allowed` | On Arch, build as a normal user. |
| `permission denied ... docker.sock` | Run `sudo usermod -aG docker $USER`, then log in again. |
| `Wayland HDR output (wp_color_management_v1): OFF` | The build has no HDR output. Install `wayland-scanner` (`libwayland-bin` on the Debian family) and the `wayland-client` development package. |
| `CMake 3.22 or higher is required` | The release is too old (for example Ubuntu 20.04 or Debian 11). Ubuntu 22.04 and Debian 12 are the oldest supported releases. |
| `.deb` install fails on `libc6 (>= 2.xx)`, `libstdc++6 (>= 13)` or `libsdl3-0` | The package was built on a newer release. Build on the target release, or use [section 4](#4-building-for-other-distributions-and-arm-with-docker). |
| The build stops, `Killed`, or `internal compiler error` | Out of memory (usually on a Raspberry Pi). Reduce parallel jobs with `DEB_BUILD_OPTIONS=parallel=2` or `cmake --build build -j2`. |

If the build works but the app misbehaves, see the
[troubleshooting section in the README](README.md#troubleshooting).
